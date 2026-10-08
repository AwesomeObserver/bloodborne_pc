// SPDX-License-Identifier: GPL-2.0-or-later
// Camera locations/fields: Supermedo/bloodborne_pc PR #3 (ported from the
// imedved mouse camera). Hooks, input transport and guest-thread updates here
// are independent implementations; no live code rewrites or polling worker.
#include "bbport_mouse.h"
#include "bbport_settings.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <cpuid.h>
#include <Zydis/Zydis.h>
#include <xbyak/xbyak.h>
#endif

namespace BbMouse {
namespace {
alignas(64) std::atomic<std::int64_t> pending_x{0}, pending_y{0};
alignas(64) std::atomic<std::uint8_t> active{0};
static_assert(sizeof(active) == 1 && decltype(active)::is_always_lock_free);
std::atomic<bool> available{false};
std::atomic<const char*> problem{"Camera hook has not been checked yet"};
uintptr_t monocular_base = 0;
constexpr double Fixed = 256.0;
constexpr std::int64_t MotionLimit = 1ll << 30;
constexpr std::array<std::size_t, 5> Sites{0x143ceaa, 0x143c6e8, 0x143c984, 0x143dde6, 0x143c870};
constexpr std::array<unsigned char, 18> MainCode{
    0xc4, 0xc1, 0x7a, 0x10, 0x85, 0x40, 0x01, 0x00, 0x00,
    0xc4, 0xc1, 0x7a, 0x10, 0x8d, 0x50, 0x01, 0x00, 0x00};

void Add(std::atomic<std::int64_t>& target, float delta) {
    const auto d = std::int64_t(std::clamp(double(delta) * Fixed,
                                         -double(MotionLimit), double(MotionLimit)));
    auto old = target.load(std::memory_order_relaxed);
    while (!target.compare_exchange_weak(old, std::clamp(old + d, -MotionLimit, MotionLimit),
                                         std::memory_order_relaxed)) {}
}
template<class T> T Read(const unsigned char* p, std::size_t offset) {
    T value;
    std::memcpy(&value, p + offset, sizeof(value));
    return value;
}
void Write(unsigned char* p, std::size_t offset, float value) {
    std::memcpy(p + offset, &value, sizeof(value));
}
float Yaw(float value) { return std::remainder(value, 6.28318530718f); }

#ifdef _WIN32
template<class T> bool Guarded(uintptr_t address, T& value) {
    SIZE_T copied = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                                        &value, sizeof(value), &copied) && copied == sizeof(value);
}
// Binocular mode uses a separate object; inspect it only when motion is pending.
bool Monocular(float dx, float dy) {
    uintptr_t p1 = 0, p2 = 0, p3 = 0;
    int enabled = 0;
    if (!Guarded(monocular_base, p1) || !p1 || !Guarded(p1 + 0x68, p2) || !p2 ||
        !Guarded(p2 + 0x84, enabled) || !enabled || !Guarded(p2 + 0x68, p3) || !p3) return false;
    struct Angles { float pitch, yaw; } angles{};
    if (!Guarded(p3 + 0x148, angles) || !std::isfinite(angles.pitch) ||
        !std::isfinite(angles.yaw)) return true;
    angles.pitch = std::clamp(angles.pitch + dy * 0.5f, -1.94f, 1.71f);
    angles.yaw = Yaw(angles.yaw + dx * 0.5f);
    SIZE_T written;
    WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(p3 + 0x148),
                       &angles, sizeof(angles), &written);
    return true;
}

bool StoreInstruction(const unsigned char* p) {
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction inst{};
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, p, 9, &inst, operands)) ||
        inst.length != 9 || inst.mnemonic != ZYDIS_MNEMONIC_VMOVSS ||
        operands[0].type != ZYDIS_OPERAND_TYPE_MEMORY ||
        operands[0].mem.base != ZYDIS_REGISTER_R13 ||
        operands[0].mem.index != ZYDIS_REGISTER_NONE ||
        operands[1].type != ZYDIS_OPERAND_TYPE_REGISTER) return false;
    const auto offset = operands[0].mem.disp.value;
    return offset == 0x140 || offset == 0x144 || offset == 0x150 ||
           offset == 0x26c || offset == 0x270;
}

void* AllocateNear(unsigned char* image, std::uint64_t size) {
    const auto base = reinterpret_cast<uintptr_t>(image);
    auto address = (base + size + 65535) & ~uintptr_t(65535);
    const auto end = base + 0x70000000;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) break;
        if (region.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(address), 4096,
                                       MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return p;
        }
        address = std::max(address + 65536,
            (reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize + 65535) &
                ~uintptr_t(65535));
    }
    return nullptr;
}

// All user extended state, including YMM upper halves and MXCSR, survives the
// native callback. The guest's SysV red zone and flags survive too. Never patch
// a running instruction: F4 changes only the atomic gate in the permanent hooks.
void Bridge(Xbyak::CodeGenerator& c, unsigned state_size, std::uint64_t mask) {
    using namespace Xbyak::util;
    const std::array<Xbyak::Reg64, 15> regs{rax, rcx, rdx, rbx, rbp, rsi, rdi,
                                           r8, r9, r10, r11, r12, r13, r14, r15};
    c.lea(rsp, ptr[rsp - 128]);
    c.pushfq();
    for (auto reg : regs) c.push(reg);
    c.mov(rbx, rsp);
    c.and_(rsp, -64);
    unsigned space = ((state_size + 63) & ~63u) + 64;
    while (space > 4096) {
        c.sub(rsp, 4096);
        c.test(byte[rsp], 0); // touch Windows guard pages in order
        space -= 4096;
    }
    c.sub(rsp, space);
    for (unsigned n = 0; n < 64; n += 8) c.mov(qword[rsp + 64 + 512 + n], 0);
    c.mov(eax, std::uint32_t(mask));
    c.mov(edx, std::uint32_t(mask >> 32));
    const unsigned char save[]{0x48, 0x0f, 0xae, 0x64, 0x24, 0x40}; // xsave64 [rsp+64]
    c.db(save, sizeof(save));
    c.mov(rcx, r13);
    c.mov(rax, reinterpret_cast<uintptr_t>(&Apply));
    c.call(rax); // Win64 shadow space is below the XSAVE area
    c.mov(eax, std::uint32_t(mask));
    c.mov(edx, std::uint32_t(mask >> 32));
    const unsigned char restore[]{0x48, 0x0f, 0xae, 0x6c, 0x24, 0x40}; // xrstor64 [rsp+64]
    c.db(restore, sizeof(restore));
    c.mov(rsp, rbx);
    for (auto it = regs.rbegin(); it != regs.rend(); ++it) c.pop(*it);
    c.popfq();
    c.lea(rsp, ptr[rsp + 128]);
}
#endif
} // namespace

bool Available() { return available.load(std::memory_order_acquire); }
const char* Problem() { return problem.load(std::memory_order_acquire); }
void SetActive(bool value) {
    value &= Available() && BbSettings::Get().mouse_camera.load();
    if (active.load(std::memory_order_relaxed) == value) return;
    if (active.exchange(value, std::memory_order_acq_rel) != value) {
        pending_x.exchange(0, std::memory_order_relaxed);
        pending_y.exchange(0, std::memory_order_relaxed);
    }
}
void Motion(float dx, float dy) {
    if (!active.load(std::memory_order_acquire) || !std::isfinite(dx) || !std::isfinite(dy)) return;
    Add(pending_x, dx);
    Add(pending_y, dy);
}
void Apply(void* pointer) {
    if (!active.load(std::memory_order_acquire)) return;
    const auto x = pending_x.exchange(0, std::memory_order_relaxed);
    const auto y = pending_y.exchange(0, std::memory_order_relaxed);
    if ((!x && !y) || !pointer) return;
    auto* camera = static_cast<unsigned char*>(pointer);
    if (Read<float>(camera, 0x154) == 1.f) return; // lock-on keeps the game's own camera
    const auto& s = BbSettings::Get();
    const float setting = s.mouse_sensitivity;
    const float scale = (std::isfinite(setting) ? std::clamp(setting, 1.f, 400.f) : 100.f) /
                        (100.f * 900.f * float(Fixed));
    const float dx = float(x) * scale, dy = float(y) * scale * (s.mouse_invert_y ? -1.f : 1.f);
#ifdef _WIN32
    if (Monocular(dx, dy)) return;
#endif
    const float pitch = Read<float>(camera, 0x140), yaw = Read<float>(camera, 0x144);
    if (!std::isfinite(pitch) || !std::isfinite(yaw)) return;
    float low = Read<float>(camera, 0x1f0), high = Read<float>(camera, 0x1ec);
    if (!std::isfinite(low) || !std::isfinite(high) || low >= high || low < -3.f || high > 3.f) {
        low = -1.94f; high = 1.71f;
    }
    const float p = std::clamp(pitch + dy, low, high), a = Yaw(yaw + dx);
    Write(camera, 0x140, p); Write(camera, 0x150, p); Write(camera, 0x26c, p);
    Write(camera, 0x144, a); Write(camera, 0x270, a);
}

bool Install(unsigned char* image, std::uint64_t size) {
#ifdef _WIN32
    if (Available()) return true;
    const auto fail = [](const char* reason) { problem = reason; return false; };
    if (size < 0x553e8d8 || !image) return fail("Mouse camera needs the supported Bloodborne 1.09 image");
    if (std::memcmp(image + Sites[0], MainCode.data(), MainCode.size()))
        return fail("Camera instruction signature differs (game version or conflicting patch)");
    for (unsigned i = 1; i < Sites.size(); ++i) {
        if (!StoreInstruction(image + Sites[i]))
            return fail("Camera store signature differs (game version or conflicting patch)");
    }
    unsigned a, b, c, d;
    __cpuid(1, a, b, c, d);
    if (!(c & (1u << 27))) return fail("OS extended register state is unavailable");
    unsigned lo, hi;
    asm volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    const auto mask = std::uint64_t(lo) | std::uint64_t(hi) << 32;
    __cpuid_count(0xd, 0, a, b, c, d);
    if (b < 576 || b > 65536) return fail("Unsupported extended register state size");
    auto* memory = static_cast<unsigned char*>(AllocateNear(image, size));
    if (!memory) return fail("Cannot allocate nearby camera hook memory");
    std::array<std::int32_t, Sites.size()> jumps{};
    try {
        Xbyak::CodeGenerator code(4096, memory);
        using namespace Xbyak::util;
        for (unsigned i = 0; i < Sites.size(); ++i) {
            code.align(16);
            auto* start = code.getCurr();
            const auto displacement = reinterpret_cast<intptr_t>(start) -
                                      reinterpret_cast<intptr_t>(image + Sites[i] + 5);
            if (displacement < INT32_MIN || displacement > INT32_MAX) throw Xbyak::Error(Xbyak::ERR_OFFSET_IS_TOO_BIG);
            jumps[i] = std::int32_t(displacement);
            Xbyak::Label original, done;
            code.lea(rsp, ptr[rsp - 128]);
            code.pushfq(); code.push(rax);
            code.mov(rax, reinterpret_cast<uintptr_t>(&active));
            code.cmp(byte[rax], 0);
            code.je(original, Xbyak::CodeGenerator::T_NEAR);
            if (i) {
                code.cmp(dword[r13 + 0x154], 0x3f800000);
                code.je(original, Xbyak::CodeGenerator::T_NEAR);
            } else {
                Xbyak::Label input;
                code.mov(rax, reinterpret_cast<uintptr_t>(&pending_x));
                code.cmp(qword[rax], 0);
                code.jne(input);
                code.mov(rax, reinterpret_cast<uintptr_t>(&pending_y));
                code.cmp(qword[rax], 0);
                code.je(original, Xbyak::CodeGenerator::T_NEAR);
                code.L(input);
            }
            code.pop(rax); code.popfq();
            code.lea(rsp, ptr[rsp + 128]);
            if (!i) {
                Bridge(code, b, mask);
                code.db(image + Sites[i], MainCode.size());
            }
            code.jmp(done, Xbyak::CodeGenerator::T_NEAR);
            code.L(original);
            code.pop(rax); code.popfq();
            code.lea(rsp, ptr[rsp + 128]);
            code.db(image + Sites[i], i ? 9 : MainCode.size());
            code.L(done);
            code.jmp(image + Sites[i] + (i ? 9 : MainCode.size()));
        }
        code.ready();
        DWORD old;
        if (!VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old)) {
            VirtualFree(memory, 0, MEM_RELEASE);
            return fail("Cannot protect camera hook memory");
        }
        FlushInstructionCache(GetCurrentProcess(), memory, code.getSize());
    } catch (const Xbyak::Error&) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return fail("Cannot generate camera hooks");
    }
    // All five sites were validated first. The loader owns writable image pages
    // and has not entered guest code, so the patch cannot race execution.
    for (unsigned i = 0; i < Sites.size(); ++i) {
        auto* p = image + Sites[i];
        p[0] = 0xe9;
        std::memcpy(p + 1, &jumps[i], 4);
        std::memset(p + 5, 0x90, (i ? 9 : MainCode.size()) - 5);
        FlushInstructionCache(GetCurrentProcess(), p, i ? 9 : MainCode.size());
    }
    monocular_base = reinterpret_cast<uintptr_t>(image) + 0x553e8d0;
    problem = nullptr;
    available.store(true, std::memory_order_release);
    std::puts("Mouse camera: verified startup hooks ready (F4); no polling thread");
    return true;
#else
    (void)image; (void)size;
    problem = "Native mouse camera currently supports Windows only";
    return false;
#endif
}
} // namespace BbMouse
