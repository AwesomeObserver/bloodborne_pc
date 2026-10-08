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
#include <limits>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <cpuid.h>
#include <Zydis/Zydis.h>
#include <xbyak/xbyak.h>
#include "bbport_mouse_trace.h"
#endif

namespace BbMouse {
namespace {
// Consume a mouse packet's two axes together. Separate exchanges could split a
// diagonal packet across updates when the window and camera threads overlapped.
alignas(64) std::atomic<std::uint64_t> pending_motion{0};
static_assert(decltype(pending_motion)::is_always_lock_free);
alignas(64) std::atomic<std::uint8_t> active{0};
// Capture and camera ownership are different: F4 may capture the cursor while
// the controller owns the camera. Keep the native stick path until new mouse input.
std::atomic<std::uint8_t> mouse_owns{0};
std::atomic<bool> stick_moving{false};
static_assert(sizeof(active) == 1 && decltype(active)::is_always_lock_free);
static_assert(sizeof(stick_moving) == 1 && decltype(stick_moving)::is_always_lock_free);
std::atomic<bool> available{false};
std::atomic<const char*> problem{"Camera hook has not been checked yet"};
uintptr_t monocular_base = 0;
constexpr double Fixed = 256.0;
constexpr std::int64_t MotionLimit = 1ll << 30;
constexpr std::array<std::size_t, 5> Sites{0x143ceaa, 0x143c6e8, 0x143c984, 0x143dde6, 0x143c870};
constexpr std::array<unsigned char, 18> MainCode{
    0xc4, 0xc1, 0x7a, 0x10, 0x85, 0x40, 0x01, 0x00, 0x00,
    0xc4, 0xc1, 0x7a, 0x10, 0x8d, 0x50, 0x01, 0x00, 0x00};
// Supported 1.09 camera update routine. The four fixed stores above only
// disable movement auto-rotation; the other stores also apply controller
// response and interpolation. They yield only while the mouse owns the camera.
constexpr std::size_t FunctionBegin = 0x143ac60, FunctionEnd = 0x143fad0;
// Confirmed by the 2026-10-08 gameplay trace: this blends the final orbit
// position with per-axis chase rates and a horizontal angular dead band.
// Equal current angles alone do not bypass it. Override only its local blend
// operand, after collision queries and before radial distance/collision handling.
constexpr std::size_t OrbitBlend = 0x143e7e6;
constexpr unsigned char OrbitCode[]{
    0xc4,0xc2,0x79,0x18,0x8d,0x30,0x01,0x00,0x00, // vbroadcastss xmm1,[r13+130]
    0xc5,0xf8,0x59,0xc1, 0xc5,0xd8,0x58,0xc0, // k += (1-k) * override
    0xc5,0xf8,0x28,0xa5,0xb0,0xfd,0xff,0xff, // old orbit position
    0xc5,0x90,0x5c,0xcc, 0xc5,0xf0,0x59,0xc0, 0xc5,0xd8,0x58,0xc0};
constexpr unsigned ExpectedStoreSignatures = 29;
constexpr std::size_t TrampolineSize = 16384;

void Add(float dx, float dy) {
    const auto counts = [](float value) {
        return std::int64_t(std::clamp(double(value) * Fixed,
                                     -double(MotionLimit), double(MotionLimit)));
    };
    const auto x = counts(dx), y = counts(dy);
    auto old = pending_motion.load(std::memory_order_relaxed);
    for (;;) {
        const auto a = std::clamp(std::int64_t(std::int32_t(old)) + x, -MotionLimit, MotionLimit);
        const auto b = std::clamp(std::int64_t(std::int32_t(old >> 32)) + y, -MotionLimit, MotionLimit);
        const auto next = std::uint64_t(std::uint32_t(a)) | std::uint64_t(std::uint32_t(b)) << 32;
        if (pending_motion.compare_exchange_weak(old, next, std::memory_order_relaxed)) return;
    }
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
// Binocular mode uses a separate target-angle object. Keep the normal camera's
// angle state synchronized with it, including idle updates and mode transitions.
bool Monocular(float dx, float dy, float& pitch, float& yaw) {
    uintptr_t p1 = 0, p2 = 0, p3 = 0;
    int enabled = 0;
    if (!Guarded(monocular_base, p1) || !p1 || !Guarded(p1 + 0x68, p2) || !p2 ||
        !Guarded(p2 + 0x84, enabled) || !enabled || !Guarded(p2 + 0x68, p3) || !p3) return false;
    struct Angles { float pitch, yaw; } angles{};
    if (!Guarded(p3 + 0x148, angles) || !std::isfinite(angles.pitch) ||
        !std::isfinite(angles.yaw)) {
        pitch = yaw = std::numeric_limits<float>::quiet_NaN();
        return true;
    }
    angles.pitch = std::clamp(angles.pitch + dy * 0.5f, -1.94f, 1.71f);
    angles.yaw = Yaw(angles.yaw + dx * 0.5f);
    SIZE_T written = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(p3 + 0x148),
                           &angles, sizeof(angles), &written) || written != sizeof(angles)) {
        pitch = yaw = std::numeric_limits<float>::quiet_NaN();
        return true;
    }
    pitch = angles.pitch; yaw = angles.yaw;
    return true;
}

bool StoreInstruction(const unsigned char* p, bool angles_only = false) {
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction inst{};
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, p, 9, &inst, operands)) ||
        inst.length != 9 || inst.mnemonic != ZYDIS_MNEMONIC_VMOVSS ||
        operands[0].type != ZYDIS_OPERAND_TYPE_MEMORY ||
        operands[0].mem.base != ZYDIS_REGISTER_R13 ||
        operands[0].mem.index != ZYDIS_REGISTER_NONE ||
        operands[1].type != ZYDIS_OPERAND_TYPE_REGISTER ||
        operands[1].reg.value < ZYDIS_REGISTER_XMM0 ||
        operands[1].reg.value > ZYDIS_REGISTER_XMM15) return false;
    const auto offset = operands[0].mem.disp.value;
    // The 1.09 camera also stores its rotation state at +130 and +294.
    // Checking only the angle fields we write in Apply rejects these stores.
    return offset == 0x130 || offset == 0x140 || offset == 0x144 || offset == 0x150 ||
           offset == 0x294 || (!angles_only && (offset == 0x148 || offset == 0x14c ||
                                               offset == 0x26c || offset == 0x270));
}

// MOV-immediate and VEXTRACTPS angle writes also occur in this routine.
// Decode whole instructions; these encodings must not escape the ownership
// gate or be matched inside another instruction's displacement/immediate.
bool AngleStore(const ZydisDecodedInstruction& inst, const ZydisDecodedOperand* operands) {
    if (operands[0].type != ZYDIS_OPERAND_TYPE_MEMORY || operands[0].size != 32 ||
        operands[0].mem.base != ZYDIS_REGISTER_R13 ||
        operands[0].mem.index != ZYDIS_REGISTER_NONE) return false;
    const auto offset = operands[0].mem.disp.value;
    if (offset != 0x140 && offset != 0x144 && offset != 0x148 && offset != 0x14c &&
        offset != 0x150 && offset != 0x26c && offset != 0x270 && offset != 0x294) return false;
    if (inst.mnemonic == ZYDIS_MNEMONIC_MOV)
        return inst.length == 11 && operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
    if (operands[1].type != ZYDIS_OPERAND_TYPE_REGISTER ||
        operands[1].reg.value < ZYDIS_REGISTER_XMM0 ||
        operands[1].reg.value > ZYDIS_REGISTER_XMM15) return false;
    return (inst.mnemonic == ZYDIS_MNEMONIC_VMOVSS && inst.length == 9) ||
           (inst.mnemonic == ZYDIS_MNEMONIC_VEXTRACTPS && inst.length == 10 &&
            operands[2].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operands[2].imm.value.u <= 3);
}

// Relocate a complete, position-independent prologue, never an arbitrary five
// bytes. Also verify that this routine takes its R13 camera from SysV's RDI.
std::size_t EntrySpan(const unsigned char* image) {
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    std::size_t copied = 0;
    bool camera_argument = false;
    for (std::size_t at = 0; at < 128;) {
        ZydisDecodedInstruction inst{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, image + FunctionBegin + at,
                128 - at, &inst, operands))) return 0;
        if (copied < 5) {
            bool safe = inst.mnemonic == ZYDIS_MNEMONIC_NOP ||
                        inst.mnemonic == ZYDIS_MNEMONIC_ENDBR64;
            if (inst.mnemonic == ZYDIS_MNEMONIC_PUSH)
                safe = operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER;
            if (inst.mnemonic == ZYDIS_MNEMONIC_MOV)
                safe = operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                       operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER;
            if (inst.mnemonic == ZYDIS_MNEMONIC_SUB)
                safe = operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                       operands[0].reg.value == ZYDIS_REGISTER_RSP &&
                       operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE;
            if (!safe) return 0;
            copied += inst.length;
        }
        if (inst.mnemonic == ZYDIS_MNEMONIC_MOV &&
            operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            operands[0].reg.value == ZYDIS_REGISTER_R13 &&
            operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            operands[1].reg.value == ZYDIS_REGISTER_RDI)
            camera_argument = true;
        if (camera_argument && copied >= 5) return copied;
        if (inst.meta.category == ZYDIS_CATEGORY_CALL ||
            inst.meta.category == ZYDIS_CATEGORY_COND_BR ||
            inst.meta.category == ZYDIS_CATEGORY_UNCOND_BR ||
            inst.meta.category == ZYDIS_CATEGORY_RET) return 0;
        at += inst.length;
    }
    return 0;
}

bool RotationPatch(const unsigned char* p) {
    // The bundled "Disable Camera Auto Rotation via Movement" patch replaces
    // these exact sites with nine single-byte NOPs. Leave those sites untouched,
    // including when F4 is off or the game enters lock-on.
    return std::all_of(p, p + 9, [](unsigned char value) { return value == 0x90; });
}

void LogSignature(const unsigned char* image, std::size_t site, std::size_t size) {
    std::fprintf(stderr, "Mouse camera: rejected image+0x%zx; bytes:", site);
    for (std::size_t n = 0; n < size; ++n) std::fprintf(stderr, " %02x", image[site + n]);
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction inst{};
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
    if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, image + site, size, &inst, operands))) {
        ZydisFormatter formatter;
        ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
        char text[256]{};
        if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&formatter, &inst, operands,
                inst.operand_count_visible, text, sizeof(text), site, ZYAN_NULL)))
            std::fprintf(stderr, "; %s", text);
    }
    std::fputc('\n', stderr);
}

void* AllocateNear(unsigned char* image, std::uint64_t size) {
    const auto base = reinterpret_cast<uintptr_t>(image);
    auto address = (base + size + 65535) & ~uintptr_t(65535);
    const auto end = base + 0x70000000;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) break;
        if (region.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(address), TrampolineSize,
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
void Bridge(Xbyak::CodeGenerator& c, unsigned state_size, std::uint64_t mask,
            const Xbyak::Reg64& camera) {
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
    c.mov(rcx, camera);
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
        mouse_owns.store(0, std::memory_order_release);
        pending_motion.exchange(0, std::memory_order_relaxed);
    }
}
void Motion(float dx, float dy) {
    if (!active.load(std::memory_order_acquire) || stick_moving.load(std::memory_order_acquire) ||
        !std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.f && dy == 0.f)) return;
    Add(dx, dy);
    mouse_owns.store(1, std::memory_order_release);
}
void Stick(std::uint8_t x, std::uint8_t y) {
    // Ownership threshold only; the actual axes reaching the game are untouched.
    // Ignore resting-stick noise, including the common 127 rather than 128 centre.
    const bool moving = std::abs(int(x) - 128) > 8 || std::abs(int(y) - 128) > 8;
    stick_moving.store(moving, std::memory_order_release);
    if (moving) {
        mouse_owns.store(0, std::memory_order_release);
        pending_motion.exchange(0, std::memory_order_relaxed);
    }
}
void Pad(std::uint8_t lx, std::uint8_t ly, std::uint8_t rx, std::uint8_t ry, std::uint32_t buttons) {
    Stick(rx, ry);
#ifdef _WIN32
    Trace::Pad(lx, ly, rx, ry, buttons);
#else
    (void)lx; (void)ly; (void)buttons;
#endif
}
void Apply(void* pointer) {
    const bool captured = active.load(std::memory_order_acquire);
    const auto motion = captured ? pending_motion.exchange(0, std::memory_order_relaxed) : 0;
#ifdef _WIN32
    const auto& settings = BbSettings::Get();
    Trace::Sample sample(pointer, motion, settings.mouse_sensitivity,
        unsigned(captured) | unsigned(mouse_owns.load(std::memory_order_acquire) != 0) << 1 |
        unsigned(stick_moving.load(std::memory_order_acquire)) << 2 | unsigned(settings.mouse_invert_y.load()) << 3);
#endif
    if (!captured) return;
    const auto x = std::int32_t(motion), y = std::int32_t(motion >> 32);
    if (!pointer || !mouse_owns.load(std::memory_order_acquire) ||
        stick_moving.load(std::memory_order_acquire)) return;
    auto* camera = static_cast<unsigned char*>(pointer);
    if (Read<float>(camera, 0x154) == 1.f) {
        mouse_owns.store(0, std::memory_order_release);
        return; // lock-on keeps the game's own camera
    }
    const auto& s = BbSettings::Get();
    const float setting = s.mouse_sensitivity;
    const float scale = (std::isfinite(setting) ? std::clamp(setting, 1.f, 400.f) : 100.f) /
                        (100.f * 900.f * float(Fixed));
    const float dx = float(x) * scale, dy = float(y) * scale * (s.mouse_invert_y ? -1.f : 1.f);
    const float pitch = Read<float>(camera, 0x140), yaw = Read<float>(camera, 0x144);
    if (!std::isfinite(pitch) || !std::isfinite(yaw)) return;
    float low = Read<float>(camera, 0x1f0), high = Read<float>(camera, 0x1ec);
    if (!std::isfinite(low) || !std::isfinite(high) || low >= high || low < -3.f || high > 3.f) {
        low = -1.94f; high = 1.71f;
    }
    float p = std::clamp(pitch + dy, low, high), a = Yaw(yaw + dx);
#ifdef _WIN32
    Monocular(dx, dy, p, a);
    if (!std::isfinite(p) || !std::isfinite(a)) return;
#endif
    // Seed the whole angle state before either axis is evaluated. Updating only
    // current angles halfway through the routine left stale targets/corrections
    // available to its pitch/yaw chase paths, even after input had stopped.
    Write(camera, 0x140, p); Write(camera, 0x148, p);
    Write(camera, 0x150, p); Write(camera, 0x26c, p);
    Write(camera, 0x144, a); Write(camera, 0x14c, a); Write(camera, 0x270, a);
    // +130 is an orbit-chase override, not angular velocity. Leave its native
    // timer/state alone; the orbit hook supplies unity only at the blend itself.
    Write(camera, 0x294, 0.f);
}

bool Install(unsigned char* image, std::uint64_t size) {
#ifdef _WIN32
    if (Available()) return true;
    const auto fail = [](const char* reason) {
        std::fprintf(stderr, "Mouse camera unavailable: %s\n", reason);
        problem = reason;
        return false;
    };
    if (size < 0x553e8d8 || !image) return fail("Mouse camera needs the supported Bloodborne 1.09 image");
    if (std::memcmp(image + Sites[0], MainCode.data(), MainCode.size())) {
        LogSignature(image, Sites[0], MainCode.size());
        return fail("Camera instruction signature differs (game version or conflicting patch)");
    }
    if (std::memcmp(image + OrbitBlend, OrbitCode, sizeof(OrbitCode))) {
        LogSignature(image, OrbitBlend, sizeof(OrbitCode));
        return fail("Camera orbit blend signature differs (game version or conflicting patch)");
    }
    std::array<bool, Sites.size()> rotation_patched{};
    for (unsigned i = 1; i < Sites.size(); ++i) {
        rotation_patched[i] = RotationPatch(image + Sites[i]);
        if (!rotation_patched[i] && !StoreInstruction(image + Sites[i])) {
            LogSignature(image, Sites[i], 9);
            return fail("Camera store signature differs (game version or conflicting patch)");
        }
    }
    const auto entry_size = EntrySpan(image);
    if (!entry_size) {
        LogSignature(image, FunctionBegin, 32);
        return fail("Camera update prologue differs (game version or conflicting patch)");
    }
    struct Hook { std::size_t offset, size; };
    std::vector<Hook> hooks{{FunctionBegin, entry_size}};
    unsigned signatures = unsigned(std::count(rotation_patched.begin(), rotation_patched.end(), true));
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    unsigned extracts = 0, immediates = 0;
    for (std::size_t offset = FunctionBegin; offset < FunctionEnd;) {
        const auto* p = image + offset;
        ZydisDecodedInstruction inst{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, p, FunctionEnd - offset, &inst, operands)))
            return fail("Cannot decode complete camera update routine");
        if (inst.length == 9 && p[0] == 0xc4 && p[1] == 0xc1 && p[2] == 0x7a && p[3] == 0x11 &&
            StoreInstruction(p, true)) ++signatures;
        if (AngleStore(inst, operands)) {
            hooks.push_back({offset, inst.length});
            extracts += inst.mnemonic == ZYDIS_MNEMONIC_VEXTRACTPS;
            immediates += inst.mnemonic == ZYDIS_MNEMONIC_MOV;
        }
        offset += inst.length;
    }
    if (signatures != ExpectedStoreSignatures || extracts != 2 || immediates != 5 || hooks.size() > 64) {
        std::fprintf(stderr, "Mouse camera: camera routine has %u/%u scalar signatures, %u/2 extract stores, %u/5 immediate stores (%zu angle/target stores)\n",
                     signatures, ExpectedStoreSignatures, extracts, immediates, hooks.size() - 1);
        return fail("Camera response signature differs (details in the game log)");
    }
    hooks.push_back({OrbitBlend, 9});
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
    std::vector<std::int32_t> jumps(hooks.size());
    try {
        Xbyak::CodeGenerator code(TrampolineSize, memory);
        using namespace Xbyak::util;
        Xbyak::Label unity;
        for (unsigned i = 0; i < hooks.size(); ++i) {
            const auto hook = hooks[i];
            code.align(16);
            auto* start = code.getCurr();
            const auto displacement = reinterpret_cast<intptr_t>(start) -
                                      reinterpret_cast<intptr_t>(image + hook.offset + 5);
            if (displacement < INT32_MIN || displacement > INT32_MAX) throw Xbyak::Error(Xbyak::ERR_OFFSET_IS_TOO_BIG);
            jumps[i] = std::int32_t(displacement);
            Xbyak::Label original, done, callback;
            code.lea(rsp, ptr[rsp - 128]);
            code.pushfq(); code.push(rax);
            if (!i) {
                // Opt-in tracing also samples native ownership and idle frames.
                code.mov(rax, reinterpret_cast<uintptr_t>(&Trace::enabled));
                code.cmp(byte[rax], 0);
                code.jne(callback, Xbyak::CodeGenerator::T_NEAR);
            }
            code.mov(rax, reinterpret_cast<uintptr_t>(&active));
            code.cmp(byte[rax], 0);
            code.je(original, Xbyak::CodeGenerator::T_NEAR);
            code.mov(rax, reinterpret_cast<uintptr_t>(&mouse_owns));
            code.cmp(byte[rax], 0);
            code.je(original, Xbyak::CodeGenerator::T_NEAR);
            code.mov(rax, reinterpret_cast<uintptr_t>(&stick_moving));
            code.cmp(byte[rax], 0);
            code.jne(original, Xbyak::CodeGenerator::T_NEAR);
            if (i) {
                code.cmp(dword[r13 + 0x154], 0x3f800000);
                code.je(original, Xbyak::CodeGenerator::T_NEAR);
            }
            code.L(callback);
            code.pop(rax); code.popfq();
            code.lea(rsp, ptr[rsp + 128]);
            if (!i) {
                Bridge(code, b, mask, rdi);
                code.db(image + hook.offset, hook.size);
            } else if (hook.offset == OrbitBlend) {
                // The game's formula k + (1-k)*1 gives equal, immediate XYZ
                // orbit response. Keep the original instruction for native
                // controller/lock-on ownership. No camera parameter is changed.
                code.vbroadcastss(xmm1, ptr[rip + unity]);
            }
            code.jmp(done, Xbyak::CodeGenerator::T_NEAR);
            code.L(original);
            code.pop(rax); code.popfq();
            code.lea(rsp, ptr[rsp + 128]);
            code.db(image + hook.offset, hook.size);
            code.L(done);
            code.jmp(image + hook.offset + hook.size);
        }
        code.align(4);
        code.L(unity); code.dd(0x3f800000);
        code.ready();
        DWORD old;
        if (!VirtualProtect(memory, TrampolineSize, PAGE_EXECUTE_READ, &old)) {
            VirtualFree(memory, 0, MEM_RELEASE);
            return fail("Cannot protect camera hook memory");
        }
        FlushInstructionCache(GetCurrentProcess(), memory, code.getSize());
    } catch (const Xbyak::Error&) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return fail("Cannot generate camera hooks");
    }
    Trace::Start(image, size, FunctionBegin, FunctionEnd);
    // All sites were validated first. The loader owns writable image pages
    // and has not entered guest code, so the patch cannot race execution.
    for (unsigned i = 0; i < hooks.size(); ++i) {
        const auto hook = hooks[i];
        auto* p = image + hook.offset;
        p[0] = 0xe9;
        std::memcpy(p + 1, &jumps[i], 4);
        std::memset(p + 5, 0x90, hook.size - 5);
        FlushInstructionCache(GetCurrentProcess(), p, hook.size);
    }
    monocular_base = reinterpret_cast<uintptr_t>(image) + 0x553e8d0;
    problem = nullptr;
    available.store(true, std::memory_order_release);
    std::printf("Mouse camera: verified update-entry and direct orbit hooks ready (F4); %zu angle/target stores gated, %u existing rotation-patch sites preserved; native controller handoff\n",
        hooks.size() - 2, unsigned(std::count(rotation_patched.begin(), rotation_patched.end(), true)));
    return true;
#else
    (void)image; (void)size;
    problem = "Native mouse camera currently supports Windows only";
    return false;
#endif
}
} // namespace BbMouse
