// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace Vulkan::RayGeometry {
inline u64 params_address = 0, positions_address = 0;
inline bool Requested() {
    const char *value = std::getenv("BB_RTX_PATH_TRACE");
    return value && std::strcmp(value, "1") == 0;
}
struct Camera {
    // Column-vector affine inverse view, then projection x/y scales (before viewport flips).
    std::array<float, 12> inverse_view{};
    std::array<float, 4> projection{};
    std::array<float, 16> clip_to_world{}, world_to_clip{}; // column-major, screen +Y down
    std::array<float, 3> position{};
    bool valid = false;
};
struct alignas(16) CaptureParams {
    std::array<u32, 4> range{}; // base slot, vertex count, first vertex, first instance
    std::array<u32, 4> instances{};
    std::array<float, 12> inverse_view{};
    std::array<float, 4> inverse_projection{};
};
static_assert(sizeof(CaptureParams) == 96);

inline Camera MakeCamera(const std::array<float, 12> &view, const std::array<float, 12> &inv,
                         const std::array<float, 4> &raw, const std::array<float, 4> &screen,
                         bool valid) {
    Camera c{.inverse_view = inv, .projection = raw, .valid = valid};
    if (!valid || std::abs(screen[0]) < 1e-6f || std::abs(screen[1]) < 1e-6f ||
        std::abs(screen[3]) < 1e-6f)
        return Camera{};
    const std::array<float, 16> p{screen[0], 0, 0,         0,         0, screen[1], 0, 0,
                                  0,         0, screen[2], screen[3], 0, 0,         1, 0};
    const std::array<float, 16> ip{
        1 / screen[0],         0, 0, 0, 0, 1 / screen[1], 0, 0, 0, 0, 0, 1, 0, 0, 1 / screen[3],
        -screen[2] / screen[3]};
    auto affine = [](const auto &m) {
        std::array<float, 16> a{};
        std::copy(m.begin(), m.end(), a.begin());
        a[15] = 1;
        return a;
    };
    auto mul = [](const auto &a, const auto &b) {
        std::array<float, 16> out{};
        for (u32 r = 0; r < 4; r++)
            for (u32 col = 0; col < 4; col++)
                for (u32 k = 0; k < 4; k++)
                    out[col * 4 + r] +=
                        a[r * 4 + k] * b[k * 4 + col]; // transpose to GLSL column-major
        return out;
    };
    c.world_to_clip = mul(p, affine(view));
    c.clip_to_world = mul(affine(inv), ip);
    c.position = {inv[3], inv[7], inv[11]};
    return c;
}

inline bool Valid(const Camera &c) {
    if (!c.valid || std::abs(c.projection[0]) < 1e-6f || std::abs(c.projection[1]) < 1e-6f ||
        std::abs(c.projection[3]) < 1e-6f)
        return false;
    for (float x : c.inverse_view)
        if (!std::isfinite(x))
            return false;
    for (float x : c.projection)
        if (!std::isfinite(x))
            return false;
    for (float x : c.clip_to_world)
        if (!std::isfinite(x))
            return false;
    for (float x : c.world_to_clip)
        if (!std::isfinite(x))
            return false;
    return true;
}
// Convert triangle-list indices to the capture allocation; reject the whole draw on bad input.
template <class Index>
bool Indices(std::span<const Index> source, u32 first_index, u32 vertices, u32 base, u32 instances,
             std::vector<u32> &out, u32 capacity) {
    if (source.empty() || source.size() % 3 || !vertices || !instances ||
        u64(source.size()) * instances > capacity - std::min<u64>(out.size(), capacity) ||
        u64(base) + u64(vertices) * instances > UINT32_MAX)
        return false;
    for (Index x : source)
        if (u64(x) < first_index || u64(x) - first_index >= vertices)
            return false;
    for (u32 i = 0; i < instances; ++i)
        for (Index x : source)
            out.push_back(base + i * vertices + u32(x) - first_index);
    return true;
}
} // namespace Vulkan::RayGeometry
