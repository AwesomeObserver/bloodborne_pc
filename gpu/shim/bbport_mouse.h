// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace BbMouse {
// Called by the loader while the image is writable, before any guest code runs.
bool Install(unsigned char* image, std::uint64_t size);
bool Available();
const char* Problem();
// Window thread: raw SDL relative motion and capture transitions. No sampler thread.
void SetActive(bool active);
void Motion(float dx, float dy);
// Final pad state, including keyboard look bindings. A deliberate stick movement
// returns camera ownership to the game; mouse motion takes it back after release.
void Stick(std::uint8_t x, std::uint8_t y);
void Pad(std::uint8_t lx, std::uint8_t ly, std::uint8_t rx, std::uint8_t ry, std::uint32_t buttons);
// Runs on the guest camera thread, before its update prologue/angle calculations.
void Apply(void* camera);
} // namespace BbMouse
