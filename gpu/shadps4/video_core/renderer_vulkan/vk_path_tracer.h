// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/renderer_vulkan/ray_geometry.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"
#include <memory>
#include <span>

namespace VideoCore {
class TextureCache;
}
namespace Vulkan {
class Instance;
class Scheduler;
class CameraMotion;
class Runtime;
class SceneTargets;
class PathTracer {
  public:
    PathTracer(const Instance &, Scheduler &, CameraMotion &, VideoCore::TextureCache &, Runtime &,
               SceneTargets &);
    PathTracer(const Instance &, Scheduler &); // headless GPU regression fixture
    ~PathTracer();
    bool Enabled() const;
    void BeginFrame();
    // Index data is copied now, never retained from guest memory.
    u32 Capture(std::span<const u16>, u32 base_vertex, u32 instances, u32 first_instance);
    u32 Capture(std::span<const u32>, u32 base_vertex, u32 instances, u32 first_instance);
    u32 CaptureNonIndexed(u32 vertices, u32 base_vertex, u32 instances, u32 first_instance);
    void SetAlbedo(VideoCore::ImageId, VideoCore::ImageId depth);
    bool Render(VideoCore::ImageId scene);
    // Synthetic GPU test enters through the same allocation/build/render path as gameplay.
    void TestScene(std::span<const std::array<float, 4>> vertices, std::span<const u32> indices);
    bool RenderImages(const RayGeometry::Camera &, vk::ImageView scene, vk::ImageView albedo,
                      vk::ImageView depth, u32 width, u32 height);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace Vulkan
