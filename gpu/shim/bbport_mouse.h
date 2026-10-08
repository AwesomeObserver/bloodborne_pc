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
// Runs only on the guest camera thread, before the original camera loads.
void Apply(void* camera);
} // namespace BbMouse
