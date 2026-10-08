// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_mouse_trace.h"
#include "test_assert.h"
#include <thread>

int main(int argc, char** argv) {
    const bool disabled = argc > 1 && !std::strcmp(argv[1], "disabled");
    const bool blocked = argc > 1 && !std::strcmp(argv[1], "blocked");
    _putenv_s("BB_MOUSE_TRACE", disabled ? "0" : "1");
    if (blocked) {
        FILE* file = std::fopen("logs", "wb");
        assert(file); std::fclose(file);
    }
    std::array<unsigned char, 128> image{};
    image.fill(0x90);
    image[0] = 0xe8; // call image+80
    const std::int32_t call = 75;
    std::memcpy(image.data() + 1, &call, 4);
    // movss xmm0,[rip+89] -> image+102
    const unsigned char load[]{0xf3,0x0f,0x10,0x05,89,0,0,0};
    std::memcpy(image.data() + 5, load, sizeof(load));
    image[13] = image[80] = 0xc3;
    BbMouse::Trace::Start(image.data(), image.size(), 0, 64);
    if (disabled || blocked) {
        assert(!BbMouse::Trace::enabled.load());
        assert(!BbMouse::Trace::mapping);
        if (disabled) assert(!std::filesystem::exists("logs"));
        return 0;
    }
    using namespace BbMouse::Trace;
    assert(enabled.load());
    assert(!std::memcmp(mapping, "BBMOUSE1", 8));
    std::array<unsigned char, 0x298> camera{};
    const float before = .25f, after = -.5f;
    for (auto offset : Fields) std::memcpy(camera.data() + offset, &before, 4);
    { Sample ignored(camera.data(), 0, 100.f, 3); }
    assert(next.load() == 0); // title/menu waiting cannot exhaust the budget
    const auto motion = std::uint64_t(std::uint32_t(-256)) | std::uint64_t(512) << 32;
    {
        Sample first(camera.data(), motion, 125.f, 11);
        for (auto offset : AfterFields) std::memcpy(camera.data() + offset, &after, 4);
    }
    auto* first = reinterpret_cast<Record*>(mapping + HeaderSize);
    assert(first->sequence == 1 && first->dx == -256 && first->dy == 512);
    assert(first->ticks && first->sensitivity == 125.f && first->flags == 11);
    for (auto field : first->before) assert(field == before);
    for (auto field : first->after) assert(field == after);
    { Sample native(camera.data(), 0, 100.f, 0); }
    auto* second = first + 1;
    assert(second->sequence == 2 && second->flags == 0 && second->dx == 0);
    // Distinct slots are safe even if two camera callbacks overlap. Collection
    // cannot write past the fixed budget or block on a logger/allocator.
    const auto fill = [&] { for (unsigned n = 0; n < Capacity; ++n) { Sample sample(camera.data(), 0, 100.f, 3); } };
    std::thread a(fill), b(fill);
    a.join(); b.join();
    assert(!enabled.load());
    for (unsigned n = 0; n < Capacity; ++n) assert(first[n].sequence == n + 1);
    if (argc > 1 && !std::strcmp(argv[1], "terminate"))
        TerminateProcess(GetCurrentProcess(), 8); // no destructor or explicit flush
    assert(FlushViewOfFile(mapping, 0));
    std::puts("Mouse trace: disabled/failure paths, signed packets, pre/post state, native ownership and bounded concurrent collection passed");
}
