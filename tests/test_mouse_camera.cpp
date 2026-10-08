// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the actual generated hooks against a synthetic 1.09 camera function.
#include <windows.h>
#include <xbyak/xbyak.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include "bbport_mouse.h"
#include "bbport_settings.h"
#include "test_assert.h"

constexpr std::size_t ImageSize = 0x5540000;
constexpr std::array<std::size_t, 5> Sites{0x143ceaa, 0x143c6e8, 0x143c984, 0x143dde6, 0x143c870};
constexpr unsigned char Loads[]{0xc4,0xc1,0x7a,0x10,0x85,0x40,1,0,0,
                               0xc4,0xc1,0x7a,0x10,0x8d,0x50,1,0,0};
struct Report {
    float pitch, smooth;
    std::uint64_t flags, r10;
    std::array<unsigned char,32> ymm;
    std::uint64_t redzone;
    std::uint32_t mxcsr;
};
alignas(32) const std::array<std::uint64_t,4> Pattern{0x123456789abcdef0, 0xfedcba9876543210,
                                                   0x1020304050607080, 0x9988776655443322};
using Run = void (*)(void*, Report*);

struct Caller : Xbyak::CodeGenerator {
    explicit Caller(void* function) {
        using namespace Xbyak::util;
        push(r12); push(r13); sub(rsp, 104);
        vmovdqu(ptr[rsp + 32], ymm15);
        mov(r13, rcx); mov(r12, rdx);
        mov(rax, reinterpret_cast<uintptr_t>(Pattern.data()));
        vmovdqu(ymm15, ptr[rax]);
        mov(eax, 0x3f600000); vmovd(xmm2, eax); // 0.875 for the original camera store
        mov(r10, 0x12349876abcdef55);
        mov(r11, 0x9876543210abcdef);
        mov(ptr[rsp - 136], r11); // lowest address in the guest call's SysV red zone
        stmxcsr(ptr[rsp + 64]);
        mov(dword[rsp + 68], 0x1f80);
        ldmxcsr(ptr[rsp + 68]);
        mov(rax, reinterpret_cast<uintptr_t>(function));
        stc(); call(rax);
        pushfq(); pop(rax);
        mov(ptr[r12 + offsetof(Report, flags)], rax);
        mov(ptr[r12 + offsetof(Report, r10)], r10);
        mov(r11, ptr[rsp - 136]);
        mov(ptr[r12 + offsetof(Report, redzone)], r11);
        stmxcsr(ptr[r12 + offsetof(Report, mxcsr)]);
        ldmxcsr(ptr[rsp + 64]);
        vmovdqu(ptr[r12 + offsetof(Report, ymm)], ymm15);
        vmovss(ptr[r12 + offsetof(Report, pitch)], xmm0);
        vmovss(ptr[r12 + offsetof(Report, smooth)], xmm1);
        vmovdqu(ymm15, ptr[rsp + 32]);
        add(rsp, 104); pop(r13); pop(r12); ret();
        ready();
    }
};

template<class T> void Put(std::array<unsigned char, 0x280>& camera, std::size_t at, T value) {
    std::memcpy(camera.data() + at, &value, sizeof(value));
}
float Get(const std::array<unsigned char, 0x280>& camera, std::size_t at) {
    float value;
    std::memcpy(&value, camera.data() + at, sizeof(value));
    return value;
}
void Check(const Report& report) {
    assert(report.flags & 1); // original flags survive every gate/callback
    assert(report.r10 == 0x12349876abcdef55);
    assert(report.redzone == 0x9876543210abcdef);
    assert(report.mxcsr == 0x1f80);
    assert(!std::memcmp(report.ymm.data(), Pattern.data(), 32)); // including YMM upper half
}

int main() {
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, ImageSize,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(image);
    std::memcpy(image + Sites[0], Loads, sizeof(Loads));
    image[Sites[0] + sizeof(Loads)] = 0xc3;
    for (unsigned i = 1; i < Sites.size(); ++i) {
        unsigned char store[]{0xc4,0xc1,0x7a,0x11,0x95,0x40,1,0,0};
        std::memcpy(image + Sites[i], store, 9);
        image[Sites[i] + 9] = 0xc3;
    }
    // A changed site rejects the entire patch, without modifying any other code.
    for (unsigned bad = 0; bad < Sites.size(); ++bad) {
        image[Sites[bad]] ^= 1;
        assert(!BbMouse::Install(image, ImageSize));
        assert(image[Sites[0]] == (bad ? 0xc4 : 0xc5));
        image[Sites[bad]] ^= 1;
    }
    assert(!BbMouse::Install(image, 256));
    assert(BbMouse::Install(image, ImageSize) && BbMouse::Available());
    std::array<std::array<unsigned char,18>,5> installed{};
    for (unsigned i = 0; i < Sites.size(); ++i)
        std::memcpy(installed[i].data(), image + Sites[i], i ? 9 : 18);
    DWORD old;
    assert(VirtualProtect(image, ImageSize, PAGE_EXECUTE_READ, &old));
    FlushInstructionCache(GetCurrentProcess(), image, ImageSize);
    Caller main_caller(image + Sites[0]);
    std::array<std::unique_ptr<Caller>, 4> store_callers;
    std::array<Run, 4> stores;
    for (unsigned i = 0; i < stores.size(); ++i) {
        store_callers[i] = std::make_unique<Caller>(image + Sites[i + 1]);
        stores[i] = store_callers[i]->getCode<Run>();
    }
    auto run = main_caller.getCode<Run>();
    std::array<unsigned char, 0x280> camera{};
    Put(camera, 0x140, .2f); Put(camera, 0x144, .3f); Put(camera, 0x150, .2f);
    Put(camera, 0x1f0, -1.94f); Put(camera, 0x1ec, 1.71f);
    Report report{};
    auto& settings = BbSettings::Get();
    settings.mouse_camera = true;
    BbMouse::Motion(90,45);
    run(camera.data(), &report);
    assert(report.pitch == .2f); Check(report);
    for (auto store : stores) {
        Put(camera, 0x140, .2f);
        store(camera.data(), &report);
        assert(Get(camera, 0x140) == .875f); Check(report);
    }
    Put(camera, 0x140, .2f);
    BbMouse::SetActive(true);
    BbMouse::Motion(90,45);
    run(camera.data(), &report);
    Check(report);
    assert(std::abs(report.pitch - .25f) < 1e-6f && report.smooth == report.pitch);
    assert(std::abs(Get(camera, 0x144) - .4f) < 1e-6f);
    assert(Get(camera, 0x26c) == report.pitch && Get(camera, 0x270) == Get(camera, 0x144));
    for (auto store : stores) {
        store(camera.data(), &report);
        assert(Get(camera, 0x140) == .25f); // camera auto-rotation stores suppressed
    }
    Put(camera, 0x154, 1.f);
    BbMouse::Motion(900,900);
    run(camera.data(), &report);
    assert(Get(camera, 0x144) == .4f);
    for (auto store : stores) {
        store(camera.data(), &report);
        assert(Get(camera, 0x140) == .875f); // lock-on restores the game stores
    }
    Put(camera, 0x154, 0.f);
    run(camera.data(), &report);
    assert(Get(camera, 0x144) == .4f); // no buffered jump after unlocking

    for (int fps : {30, 60, 120, 240}) {
        Put(camera, 0x140, 0.f); Put(camera, 0x144, 0.f);
        for (int n = 0; n < 8000; ++n) {
            BbMouse::Motion(.125f, 0);
            if (n % (8000 / fps) == 0) run(camera.data(), &report);
        }
        run(camera.data(), &report);
        assert(std::abs(Get(camera, 0x144) - 1000.f / 900.f) < .0001f);
    }
    settings.mouse_invert_y = true;
    settings.mouse_sensitivity = 200.f;
    Put(camera, 0x140, 0.f);
    BbMouse::Motion(0,90); run(camera.data(), &report);
    assert(std::abs(report.pitch + .2f) < 1e-6f);
    BbMouse::Motion(0,1e30f); run(camera.data(), &report);
    assert(report.pitch == -1.94f);
    BbMouse::Motion(std::numeric_limits<float>::infinity(), 1);
    BbMouse::Motion(std::numeric_limits<float>::quiet_NaN(), 1);
    run(camera.data(), &report);
    assert(std::isfinite(report.pitch));
    BbMouse::Motion(900,900);
    BbMouse::SetActive(false); BbMouse::SetActive(true);
    const auto yaw = Get(camera, 0x144);
    run(camera.data(), &report);
    assert(Get(camera, 0x144) == yaw); // focus/menu/toggle discards old motion

    // The separate binocular object is updated without touching normal-camera angles.
    std::array<unsigned char,0x90> p1{}, p2{};
    std::array<unsigned char,0x280> binocular{};
    auto* p2_ptr = p2.data(); auto* binocular_ptr = binocular.data(); auto* p1_ptr = p1.data();
    std::memcpy(p1.data() + 0x68, &p2_ptr, 8);
    std::memcpy(p2.data() + 0x68, &binocular_ptr, 8);
    assert(VirtualProtect(image + 0x553e000, 4096, PAGE_READWRITE, &old));
    std::memcpy(image + 0x553e8d0, &p1_ptr, 8);
    const int mono_on = 1, mono_off = 0;
    std::memcpy(p2.data() + 0x84, &mono_on, 4);
    settings.mouse_sensitivity = 100.f;
    settings.mouse_invert_y = false;
    BbMouse::Motion(90,45); run(camera.data(), &report);
    assert(std::abs(Get(binocular, 0x148) - .025f) < 1e-6f);
    assert(std::abs(Get(binocular, 0x14c) - .05f) < 1e-6f);
    assert(Get(camera, 0x144) == yaw);
    std::memcpy(p2.data() + 0x84, &mono_off, 4);

    // Repeated toggles never touch instructions that a guest thread is executing.
    std::atomic<bool> stop{false};
    std::thread guest([&] { Report r{}; while (!stop) run(camera.data(), &r); });
    for (int n = 0; n < 10000; ++n) {
        BbMouse::Motion(1,0);
        BbMouse::SetActive(false); BbMouse::SetActive(true);
    }
    stop = true; guest.join();
    for (unsigned i = 0; i < Sites.size(); ++i)
        assert(!std::memcmp(installed[i].data(), image + Sites[i], i ? 9 : 18));

    const auto bench = [&](bool capture, bool moving) {
        BbMouse::SetActive(capture);
        const auto start = std::chrono::steady_clock::now();
        for (unsigned n = 0; n < 100000; ++n) {
            if (moving) BbMouse::Motion(1,0);
            run(camera.data(), &report);
        }
        return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/100000;
    };
    const double off = bench(false,false), idle = bench(true,false), moving = bench(true,true);
    std::printf("Mouse camera hook including test ABI wrapper: off %.0f ns, idle %.0f ns, motion %.0f ns\n",
                off,idle,moving);
    BbMouse::SetActive(false);
    VirtualFree(image,0,MEM_RELEASE);
    std::puts("PASS: executable camera hooks, full AVX/flags preservation, signature rollback, "
              "lock-on, frame-independent sensitivity, invalid input and concurrent toggles");
}
