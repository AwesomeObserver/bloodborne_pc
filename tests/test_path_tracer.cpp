// SPDX-License-Identifier: GPL-2.0-or-later
// Executes the production BVH, ray-query and denoising path and reads its GPU output.
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_path_tracer.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <vk_mem_alloc.h>
// Keep test_assert last: standard/Vulkan headers include assert.h again.
#include "test_assert.h"

using namespace Vulkan;
static void env(const char *name, const char *value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}
static void barrier(vk::CommandBuffer cmd) {
    const vk::MemoryBarrier2 b{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite};
    cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &b});
}
static float half(u16 v) {
    const int e = (v >> 10) & 31;
    return (v & 0x8000 ? -1.f : 1.f) * (e == 0 ? std::ldexp(float(v & 1023), -24)
                                        : e == 31
                                            ? INFINITY
                                            : std::ldexp(1.f + float(v & 1023) / 1024.f, e - 15));
}
int main(int argc, char **argv) {
    const bool disabled = argc > 1;
    env("BB_RTX_PATH_TRACE", disabled ? "0" : "1");
    env("BB_RTX_MODE", "1");
    env("BB_RTX_MAX_VERTICES", "128");
    env("BB_RTX_MAX_INDICES", "99"); // non-aligned ring stride
    env("BB_RTX_BOUNCES", "1");
    Instance instance(0, true);
    Scheduler scheduler(instance);
    PathTracer tracer(instance, scheduler);
    if (disabled) {
        assert(!tracer.Enabled() && !instance.IsRayQueryEnabled());
        assert(!RayGeometry::params_address && !RayGeometry::positions_address);
        return 0;
    }
    if (!instance.IsRayQueryEnabled())
        return 77;
    assert(tracer.Enabled());
    const auto device = instance.GetDevice();
    constexpr u32 W = 32, H = 24;
    std::array<VideoCore::UniqueImage, 3> images;
    std::array<vk::UniqueImageView, 3> views;
    for (u32 n = 0; n < 3; n++) {
        const auto format = n == 0   ? vk::Format::eR16G16B16A16Sfloat
                            : n == 1 ? vk::Format::eR8G8B8A8Srgb
                                     : vk::Format::eD32Sfloat;
        images[n] = VideoCore::UniqueImage(device, instance.GetAllocator());
        images[n].Create(
            {.imageType = vk::ImageType::e2D,
             .format = format,
             .extent = {W, H, 1},
             .mipLevels = 1,
             .arrayLayers = 1,
             .samples = vk::SampleCountFlagBits::e1,
             .tiling = vk::ImageTiling::eOptimal,
             .usage =
                 vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                 vk::ImageUsageFlagBits::eTransferDst |
                 (n == 0 ? vk::ImageUsageFlagBits::eStorage : vk::ImageUsageFlagBits::eSampled)});
        views[n] = device
                       .createImageViewUnique(
                           {.image = images[n],
                            .viewType = vk::ImageViewType::e2D,
                            .format = format,
                            .subresourceRange = {n == 2 ? vk::ImageAspectFlagBits::eDepth
                                                        : vk::ImageAspectFlagBits::eColor,
                                                 0, 1, 0, 1}})
                       .value;
    }
    VkBuffer readback{};
    VmaAllocation allocation{};
    VmaAllocationInfo mapped{};
    const VkBufferCreateInfo bi{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .size = W * H * 8,
                                .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    const VmaAllocationCreateInfo ai{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                              VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                                     .usage = VMA_MEMORY_USAGE_AUTO,
                                     .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    assert(vmaCreateBuffer(instance.GetAllocator(), &bi, &ai, &readback, &allocation, &mapped) ==
           VK_SUCCESS);
    const std::array<float, 12> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const std::array<float, 4> projection{1, 1, 1.01f, -0.0505f};
    const auto camera = RayGeometry::MakeCamera(identity, identity, projection, projection, true);
    assert(RayGeometry::Valid(camera));
    const std::array<std::array<float, 4>, 8> vertices{{{-20, -20, 5, 1},
                                                        {20, -20, 5, 1},
                                                        {20, 20, 5, 1},
                                                        {-20, 20, 5, 1},
                                                        {-10, 4, 3, 1},
                                                        {10, 4, 3, 1},
                                                        {10, 12, 3, 1},
                                                        {-10, 12, 3, 1}}};
    const std::array<u32, 12> indices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    bool initialized = false;
    const auto run = [&](float depth, bool blocker, u32 bounces, bool bad = false, u32 width = W,
                         u32 height = H) {
        env("BB_RTX_BOUNCES", bounces == 1 ? "1" : "3");
        tracer.BeginFrame();
        scheduler.Record([&](vk::CommandBuffer cmd) {
            barrier(cmd);
            for (u32 n = 0; n < 3; n++) {
                const vk::ImageSubresourceRange range{n == 2 ? vk::ImageAspectFlagBits::eDepth
                                                             : vk::ImageAspectFlagBits::eColor,
                                                      0, 1, 0, 1};
                const vk::ImageMemoryBarrier2 t{
                    .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                    .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
                    .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                    .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                    .oldLayout =
                        initialized ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .image = images[n],
                    .subresourceRange = range};
                cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &t});
                if (n == 2)
                    cmd.clearDepthStencilImage(images[n], vk::ImageLayout::eGeneral, {depth, 0},
                                               range);
                else
                    cmd.clearColorImage(
                        images[n], vk::ImageLayout::eGeneral,
                        vk::ClearColorValue{.float32 =
                                                n == 0 ? std::array<float, 4>{1, 2, 3, 0.5f}
                                                       : std::array<float, 4>{0.8f, 0.4f, 0.2f, 1}},
                        range);
            }
            barrier(cmd);
        });
        // All captures are immutable until scheduler.Finish below.
        const std::array<u32, 3> invalid{0, 1, 128};
        tracer.TestScene(vertices, bad ? std::span<const u32>(invalid)
                                       : std::span<const u32>(indices.data(), blocker ? 12 : 6));
        const bool rendered =
            tracer.RenderImages(camera, *views[0], *views[1], *views[2], width, height);
        assert(rendered != bad);
        if (rendered)
            assert(!tracer.RenderImages(camera, *views[0], *views[1], *views[2], width, height));
        scheduler.Record([&](vk::CommandBuffer cmd) {
            barrier(cmd);
            cmd.copyImageToBuffer(
                images[0], vk::ImageLayout::eGeneral, readback,
                vk::BufferImageCopy{.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                    .imageExtent = {W, H, 1}});
            const vk::MemoryBarrier2 b{.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                                       .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                                       .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                       .dstAccessMask = vk::AccessFlagBits2::eHostRead};
            cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &b});
        });
        scheduler.Finish();
        initialized = true;
        auto *values = static_cast<u16 *>(mapped.pMappedData);
        float total = 0;
        for (u32 p = 0; p < W * H; p++) {
            for (u32 x = 0; x < 4; x++)
                assert(std::isfinite(half(values[p * 4 + x])));
            assert(half(values[p * 4 + 3]) == 0.5f);
        }
        for (u32 y = H / 2 - 2; y < H / 2 + 2; y++)
            for (u32 x = W / 2 - 2; x < W / 2 + 2; x++)
                total += half(values[(y * W + x) * 4]);
        return total / 16;
    };
    const float projected = projection[2] + projection[3] / 5;
    const float direct = run(projected, false, 1), occluded = run(projected, true, 1),
                multi = run(projected, false, 3);
    std::printf("RTX GPU pixels: direct=%f shadow=%f multibounce=%f\n", direct, occluded, multi);
    assert(direct > 0.01f && direct < 1);
    assert(occluded < direct * 0.1f);
    assert(multi > direct);
    assert(run(1, false, 3) == 1);               // background, original alpha preserved
    assert(run(projected, false, 1, true) == 1); // invalid capture safely leaves the raster frame
    for (u32 i = 0; i < 8; i++)
        assert(run(projected, false, 3) > 0);                  // ring reuse
    assert(run(projected, false, 3, false, W / 2, H / 2) > 0); // denoiser recreation
    assert(run(projected, false, 3) > 0);                      // restored size
    env("BB_RTX_MODE", "2");
    assert(run(projected, false, 3) > 1); // hybrid preserves raster lighting
    vmaDestroyBuffer(instance.GetAllocator(), readback, allocation);
    std::puts("RTX path tracer GPU: PASS");
}
