// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_path_tracer.h"
#include "video_core/host_shaders/rtx_denoise_comp.h"
#include "video_core/host_shaders/rtx_path_trace_comp.h"
#include "video_core/renderer_vulkan/vk_camera_motion.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scene_resolution.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vk_mem_alloc.h>

namespace Vulkan {
namespace {
u32 Option(const char *name, u32 fallback, u32 low, u32 high) {
    const char *s = std::getenv(name);
    if (!s || !*s)
        return fallback;
    char *end{};
    const auto value = std::strtoull(s, &end, 10);
    return end && !*end ? u32(std::clamp<u64>(value, low, high)) : fallback;
}
struct Buffer {
    VmaAllocator allocator{};
    VmaAllocation allocation{};
    vk::Buffer handle{};
    u64 address{}, size{};
    void *mapped{};
    ~Buffer() {
        if (handle)
            vmaDestroyBuffer(allocator, handle, allocation);
    }
    bool Create(const Instance &i, u64 bytes, vk::BufferUsageFlags usage, bool host = false) {
        allocator = i.GetAllocator();
        size = bytes;
        VkBufferCreateInfo ci{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = bytes,
            .usage = VkBufferUsageFlags(usage | vk::BufferUsageFlagBits::eShaderDeviceAddress)};
        VmaAllocationCreateInfo ai{
            .flags = host ? VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                          : 0u,
            .usage =
                host ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        if (host)
            ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VkBuffer raw{};
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(allocator, &ci, &ai, &raw, &allocation, &info) != VK_SUCCESS)
            return false;
        handle = raw;
        mapped = info.pMappedData;
        address = i.GetDevice().getBufferAddress({.buffer = handle});
        return address != 0;
    }
};
struct Image {
    VmaAllocator allocator{};
    VmaAllocation allocation{};
    vk::Image handle{};
    vk::UniqueImageView view;
    ~Image() {
        view.reset();
        if (handle)
            vmaDestroyImage(allocator, handle, allocation);
    }
    bool Create(const Instance &i, u32 w, u32 h, vk::Format format) {
        allocator = i.GetAllocator();
        VkImage raw{};
        VkImageCreateInfo ci{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                             .imageType = VK_IMAGE_TYPE_2D,
                             .format = VkFormat(format),
                             .extent = {w, h, 1},
                             .mipLevels = 1,
                             .arrayLayers = 1,
                             .samples = VK_SAMPLE_COUNT_1_BIT,
                             .tiling = VK_IMAGE_TILING_OPTIMAL,
                             .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        VmaAllocationCreateInfo ai{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
        if (vmaCreateImage(allocator, &ci, &ai, &raw, &allocation, nullptr) != VK_SUCCESS)
            return false;
        handle = raw;
        auto result = i.GetDevice().createImageViewUnique(
            {.image = handle,
             .viewType = vk::ImageViewType::e2D,
             .format = format,
             .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
        if (result.result != vk::Result::eSuccess)
            return false;
        view = std::move(result.value);
        return true;
    }
};
struct Images {
    u32 width{}, height{};
    Image raw, motion, history[2];
};
struct alignas(16) Frame {
    std::array<float, 16> clip_world{}, world_clip{}, previous_clip{};
    std::array<float, 4> camera{}, sun{0.3f, 0.8f, -0.2f, 3.0f}, sky{0.08f, 0.11f, 0.17f, 1.0f};
    std::array<u32, 4> options{};
    std::array<float, 4> control{}, jitter{};
};
static_assert(sizeof(Frame) == 288);
void Barrier(vk::CommandBuffer c, vk::PipelineStageFlags2 src, vk::AccessFlags2 sa,
             vk::PipelineStageFlags2 dst, vk::AccessFlags2 da) {
    const vk::MemoryBarrier2 b{
        .srcStageMask = src, .srcAccessMask = sa, .dstStageMask = dst, .dstAccessMask = da};
    c.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &b});
}
} // namespace

struct PathTracer::Impl {
    const Instance &instance;
    Scheduler &scheduler;
    CameraMotion *camera{};
    VideoCore::TextureCache *textures{};
    Runtime *runtime{};
    SceneTargets *targets{};
    bool enabled = false, rendered = false, waiting_logged = false;
    const u32 max_vertices = Option("BB_RTX_MAX_VERTICES", 1u << 20, 32, 4u << 20);
    const u32 max_indices = Option("BB_RTX_MAX_INDICES", 3u << 20, 96, 12u << 20);
    static constexpr u32 Slots = 4, Params = 8192;
    u32 used_vertices{}, used_params{}, draws{}, skipped{}, frame_slot{};
    u64 frame_id{}, frame_stride{}, index_stride{}, scratch_address{}, last_render_frame{},
        render_count{};
    std::array<u64, Slots> ticks{};
    std::vector<u32> index_data;
    Buffer positions, indices, params, frames, instances, scratch, blas_storage, tlas_storage;
    vk::UniqueAccelerationStructureKHR blas, tlas;
    vk::UniqueDescriptorSetLayout trace_desc, denoise_desc;
    vk::UniquePipelineLayout trace_layout, denoise_layout;
    vk::UniquePipeline trace_pipeline, denoise_pipeline;
    std::shared_ptr<Images> images;
    VideoCore::ImageId albedo{}, albedo_depth{};
    std::array<float, 3> previous_position{};

    Impl(const Instance &i, Scheduler &s) : instance(i), scheduler(s) {
        if (!RayGeometry::Requested() || !instance.IsRayQueryEnabled())
            return;
        const auto device = instance.GetDevice();
        auto alignment = std::max<u64>(
            16,
            instance.GetPhysicalDevice().getProperties().limits.minStorageBufferOffsetAlignment);
        frame_stride = (sizeof(Frame) + alignment - 1) & ~(alignment - 1);
        index_stride = (u64(max_indices) * 4 + alignment - 1) & ~(alignment - 1);
        const auto storage = vk::BufferUsageFlagBits::eStorageBuffer;
        const auto input = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        if (!positions.Create(i, u64(max_vertices) * 16,
                              storage | input | vk::BufferUsageFlagBits::eTransferDst) ||
            !indices.Create(i, index_stride * Slots, storage | input, true) ||
            !params.Create(i, u64(1 + Params * Slots) * sizeof(RayGeometry::CaptureParams), storage,
                           true) ||
            !frames.Create(i, frame_stride * Slots, storage, true) ||
            !instances.Create(i, sizeof(vk::AccelerationStructureInstanceKHR), input, true)) {
            std::puts("RTX path tracing: capture allocation failed; raster fallback");
            return;
        }
        std::memset(params.mapped, 0, size_t(params.size));
        index_data.reserve(max_indices);
        vk::AccelerationStructureGeometryTrianglesDataKHR tri{.vertexFormat =
                                                                  vk::Format::eR32G32B32Sfloat,
                                                              .vertexData = {positions.address},
                                                              .vertexStride = 16,
                                                              .maxVertex = max_vertices - 1,
                                                              .indexType = vk::IndexType::eUint32,
                                                              .indexData = {indices.address}};
        vk::AccelerationStructureGeometryKHR geometry{.geometryType =
                                                          vk::GeometryTypeKHR::eTriangles,
                                                      .geometry = {tri},
                                                      .flags = vk::GeometryFlagBitsKHR::eOpaque};
        vk::AccelerationStructureBuildGeometryInfoKHR build{
            .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
            .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild,
            .geometryCount = 1,
            .pGeometries = &geometry};
        const u32 max_primitives = max_indices / 3;
        const auto bs = device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, build, max_primitives);
        vk::AccelerationStructureGeometryInstancesDataKHR inst{.data = {instances.address}};
        vk::AccelerationStructureGeometryKHR tg{.geometryType = vk::GeometryTypeKHR::eInstances,
                                                .geometry = {.instances = inst}};
        build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        build.pGeometries = &tg;
        const auto ts = device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice, build, 1u);
        const auto props =
            instance.GetPhysicalDevice()
                .getProperties2<vk::PhysicalDeviceProperties2,
                                vk::PhysicalDeviceAccelerationStructurePropertiesKHR>()
                .get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
        const u64 a = props.minAccelerationStructureScratchOffsetAlignment;
        if (max_primitives > props.maxPrimitiveCount ||
            !blas_storage.Create(i, bs.accelerationStructureSize,
                                 vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR) ||
            !tlas_storage.Create(i, ts.accelerationStructureSize,
                                 vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR) ||
            !scratch.Create(i, std::max(bs.buildScratchSize, ts.buildScratchSize) + a, storage)) {
            std::puts(
                "RTX path tracing: acceleration-structure allocation failed; raster fallback");
            return;
        }
        scratch_address = (scratch.address + a - 1) & ~(a - 1);
        auto b = device.createAccelerationStructureKHRUnique(
            {.buffer = blas_storage.handle,
             .size = bs.accelerationStructureSize,
             .type = vk::AccelerationStructureTypeKHR::eBottomLevel});
        auto t = device.createAccelerationStructureKHRUnique(
            {.buffer = tlas_storage.handle,
             .size = ts.accelerationStructureSize,
             .type = vk::AccelerationStructureTypeKHR::eTopLevel});
        if (b.result != vk::Result::eSuccess || t.result != vk::Result::eSuccess)
            return;
        blas = std::move(b.value);
        tlas = std::move(t.value);
        vk::AccelerationStructureInstanceKHR identity{};
        identity.transform.matrix[0][0] = identity.transform.matrix[1][1] =
            identity.transform.matrix[2][2] = 1;
        identity.mask = 255;
        identity.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        identity.accelerationStructureReference =
            device.getAccelerationStructureAddressKHR({.accelerationStructure = *blas});
        std::memcpy(instances.mapped, &identity, sizeof(identity));
        const std::array trace_types{vk::DescriptorType::eAccelerationStructureKHR,
                                     vk::DescriptorType::eStorageBuffer,
                                     vk::DescriptorType::eStorageBuffer,
                                     vk::DescriptorType::eStorageBuffer,
                                     vk::DescriptorType::eSampledImage,
                                     vk::DescriptorType::eSampledImage,
                                     vk::DescriptorType::eStorageImage,
                                     vk::DescriptorType::eSampledImage};
        const std::array denoise_types{
            vk::DescriptorType::eSampledImage, vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eSampledImage, vk::DescriptorType::eSampledImage,
            vk::DescriptorType::eStorageImage, vk::DescriptorType::eStorageImage,
            vk::DescriptorType::eStorageBuffer};
        auto pipeline = [&](auto types, std::span<const u32> code, auto &desc, auto &layout,
                            auto &result) {
            std::vector<vk::DescriptorSetLayoutBinding> bindings;
            for (u32 n = 0; n < types.size(); n++)
                bindings.push_back({.binding = n,
                                    .descriptorType = types[n],
                                    .descriptorCount = 1,
                                    .stageFlags = vk::ShaderStageFlagBits::eCompute});
            auto d = device.createDescriptorSetLayoutUnique(
                {.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
                 .bindingCount = u32(bindings.size()),
                 .pBindings = bindings.data()});
            if (d.result != vk::Result::eSuccess)
                return false;
            desc = std::move(d.value);
            auto l =
                device.createPipelineLayoutUnique({.setLayoutCount = 1, .pSetLayouts = &*desc});
            if (l.result != vk::Result::eSuccess)
                return false;
            layout = std::move(l.value);
            auto m = device.createShaderModuleUnique(
                {.codeSize = code.size_bytes(), .pCode = code.data()});
            if (m.result != vk::Result::eSuccess)
                return false;
            auto p = device.createComputePipelineUnique(
                {},
                vk::ComputePipelineCreateInfo{.stage = {.stage = vk::ShaderStageFlagBits::eCompute,
                                                        .module = *m.value,
                                                        .pName = "main"},
                                              .layout = *layout});
            if (p.result != vk::Result::eSuccess)
                return false;
            result = std::move(p.value);
            return true;
        };
        if (!pipeline(trace_types, RTX_PATH_TRACE_COMP, trace_desc, trace_layout, trace_pipeline) ||
            !pipeline(denoise_types, RTX_DENOISE_COMP, denoise_desc, denoise_layout,
                      denoise_pipeline))
            return;
        enabled = true;
        RayGeometry::params_address = params.address;
        RayGeometry::positions_address = positions.address;
        std::printf("RTX path tracing ready: native hardware ray query; capture budget %u vertices "
                    "/ %u triangles; experimental\n",
                    max_vertices, max_indices / 3);
    }
    ~Impl() {
        if (positions.handle)
            scheduler.Finish();
        if (RayGeometry::params_address == params.address) {
            RayGeometry::params_address = 0;
            RayGeometry::positions_address = 0;
        }
    }
    void Begin() {
        if (!enabled)
            return;
        if (draws && last_render_frame != frame_id && !waiting_logged) {
            std::printf("RTX capture waiting: %u draws / %zu triangles captured, but no compatible "
                        "HDR scene was traced before display; check albedo, camera and scene hook\n",
                        draws, index_data.size() / 3);
            waiting_logged = true;
        }
        ++frame_id;
        frame_slot = u32(frame_id % Slots);
        if (ticks[frame_slot]) {
            scheduler.Wait(ticks[frame_slot]);
            ticks[frame_slot] = 0;
        }
        used_vertices = used_params = draws = skipped = 0;
        index_data.clear();
        albedo = {};
        albedo_depth = {};
        scheduler.EndRendering();
        scheduler.Record([buffer = positions.handle, size = positions.size](vk::CommandBuffer c) {
            Barrier(c, vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                    vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            c.fillBuffer(buffer, 0, size, 0);
            Barrier(c, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderWrite);
        });
    }
    template <class Index>
    u32 Capture(std::span<const Index> input, u32 base_vertex, u32 count, u32 first_instance) {
        if (!enabled || !camera || input.empty() || !count || used_params >= Params) {
            ++skipped;
            return 0;
        }
        auto cam = camera->GeometryCamera();
        if (!RayGeometry::Valid(cam)) {
            ++skipped;
            return 0;
        }
        const auto [min, max] = std::minmax_element(input.begin(), input.end());
        const u64 vertex_count = u64(*max) - u64(*min) + 1;
        if (vertex_count > max_vertices || vertex_count * count > max_vertices - used_vertices ||
            !RayGeometry::Indices(input, u32(*min), u32(vertex_count), used_vertices, count,
                                  index_data, max_indices)) {
            ++skipped;
            return 0;
        }
        const u32 param_index = 1 + frame_slot * Params + used_params++;
        RayGeometry::CaptureParams p{
            .range = {used_vertices, u32(vertex_count), base_vertex + u32(*min), first_instance},
            .instances = {count, 0, 0, 0},
            .inverse_view = cam.inverse_view,
            .inverse_projection = {1 / cam.projection[0], 1 / cam.projection[1], 0, 0}};
        std::memcpy(static_cast<u8 *>(params.mapped) + u64(param_index) * sizeof(p), &p, sizeof(p));
        used_vertices += u32(vertex_count * count);
        ++draws;
        ticks[frame_slot] = scheduler.CurrentTick();
        return param_index;
    }
    bool EnsureImages(u32 w, u32 h) {
        if (images && images->width == w && images->height == h)
            return true;
        auto next = std::make_shared<Images>();
        next->width = w;
        next->height = h;
        if (!next->raw.Create(instance, w, h, vk::Format::eR16G16B16A16Sfloat) ||
            !next->motion.Create(instance, w, h, vk::Format::eR16G16Sfloat) ||
            !next->history[0].Create(instance, w, h, vk::Format::eR16G16B16A16Sfloat) ||
            !next->history[1].Create(instance, w, h, vk::Format::eR16G16B16A16Sfloat))
            return false;
        if (images)
            scheduler.DeferOperation([old = std::move(images)] {});
        images = next;
        rendered = false;
        scheduler.Record([next](vk::CommandBuffer c) {
            for (auto *image : {&next->raw, &next->motion, &next->history[0], &next->history[1]}) {
                const vk::ImageMemoryBarrier2 b{
                    .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                    .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .image = image->handle,
                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
                c.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
                c.clearColorImage(image->handle, vk::ImageLayout::eGeneral, vk::ClearColorValue{},
                                  b.subresourceRange);
            }
            Barrier(c, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
        });
        return true;
    }
    bool Draw(const RayGeometry::Camera &cam, vk::ImageView scene, vk::ImageView material,
              vk::ImageView depth, u32 w, u32 h) {
        if (!enabled || !RayGeometry::Valid(cam) || !used_vertices || index_data.empty() ||
            !scene || !material || !depth || !w || !h)
            return false;
        scheduler.EndRendering();
        if (!EnsureImages(w, h))
            return false;
        if (last_render_frame == frame_id)
            return false;
        const u64 index_offset = u64(frame_slot) * index_stride;
        std::memcpy(static_cast<u8 *>(indices.mapped) + index_offset, index_data.data(),
                    index_data.size() * 4);
        Frame frame{.clip_world = cam.clip_to_world,
                    .world_clip = cam.world_to_clip,
                    .camera = {cam.position[0], cam.position[1], cam.position[2], 0},
                    .options = {w, h, Option("BB_RTX_SAMPLES", 1, 1, 4),
                                Option("BB_RTX_BOUNCES", 3, 1, 6)}};
        const u32 mode = Option("BB_RTX_MODE", 1, 1, 2);
        frame.control = {float(frame_id), float(mode), 0, 0.5f};
        frame.jitter[2] = cam.projection[2];
        frame.jitter[3] = cam.projection[3];
        if (camera && camera->Ready()) {
            const auto fc = camera->FrameCamera();
            std::copy(std::begin(fc.clip_to_previous), std::end(fc.clip_to_previous),
                      frame.previous_clip.begin());
            frame.jitter[0] = fc.jitter[0];
            frame.jitter[1] = fc.jitter[1];
            float gap = 0;
            for (u32 x = 0; x < 3; x++)
                gap += std::abs(cam.position[x] - previous_position[x]);
            frame.control[2] =
                rendered && last_render_frame + 1 == frame_id && gap < 10 ? 1.f : 0.f;
            camera->RecordMotion(depth, *images->motion.view, w, h);
        } else {
            const auto owned = images;
            scheduler.Record([owned](vk::CommandBuffer c) {
                Barrier(c, vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                        vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
                c.clearColorImage(
                    owned->motion.handle, vk::ImageLayout::eGeneral, vk::ClearColorValue{},
                    vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
            });
        }
        std::memcpy(static_cast<u8 *>(frames.mapped) + frame_slot * frame_stride, &frame,
                    sizeof(frame));
        const vk::DescriptorBufferInfo vertex_info{positions.handle, 0, positions.size};
        const vk::DescriptorBufferInfo index_info{indices.handle, index_offset,
                                                  index_data.size() * 4};
        const vk::DescriptorBufferInfo frame_info{frames.handle, frame_slot * frame_stride,
                                                  sizeof(frame)};
        const auto owned = images;
        const u32 history = u32(frame_id % 2), primitives = u32(index_data.size() / 3);
        const auto device = instance.GetDevice();
        const auto as_b = *blas, as_t = *tlas;
        const u64 idx = indices.address + index_offset, inst = instances.address;
        const u64 pos = positions.address, scr = scratch_address;
        const u32 vertices = used_vertices;
        const auto trace = *trace_pipeline, denoise = *denoise_pipeline;
        const auto tl = *trace_layout, dl = *denoise_layout;
        scheduler.RecordCrumb({.name = "RTX path tracing"}, [=](vk::CommandBuffer c) {
            Barrier(c, vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eHost,
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite |
                        vk::AccessFlagBits2::eHostWrite,
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                    vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                        vk::AccessFlagBits2::eAccelerationStructureWriteKHR |
                        vk::AccessFlagBits2::eShaderRead);
            vk::AccelerationStructureGeometryTrianglesDataKHR tri{
                .vertexFormat = vk::Format::eR32G32B32Sfloat,
                .vertexData = {pos},
                .vertexStride = 16,
                .maxVertex = vertices - 1,
                .indexType = vk::IndexType::eUint32,
                .indexData = {idx}};
            vk::AccelerationStructureGeometryKHR bg{.geometryType = vk::GeometryTypeKHR::eTriangles,
                                                    .geometry = {tri},
                                                    .flags = vk::GeometryFlagBitsKHR::eOpaque};
            vk::AccelerationStructureBuildGeometryInfoKHR build{
                .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
                .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild,
                .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
                .dstAccelerationStructure = as_b,
                .geometryCount = 1,
                .pGeometries = &bg,
                .scratchData = {scr}};
            const vk::AccelerationStructureBuildRangeInfoKHR range{.primitiveCount = primitives};
            const auto *ranges = &range;
            c.buildAccelerationStructuresKHR(1, &build, &ranges);
            Barrier(c, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                    vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                    vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                        vk::AccessFlagBits2::eAccelerationStructureWriteKHR);
            vk::AccelerationStructureGeometryInstancesDataKHR instance_data{.data = {inst}};
            vk::AccelerationStructureGeometryKHR tg{.geometryType = vk::GeometryTypeKHR::eInstances,
                                                    .geometry = {.instances = instance_data}};
            build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
            build.dstAccelerationStructure = as_t;
            build.pGeometries = &tg;
            const vk::AccelerationStructureBuildRangeInfoKHR one{.primitiveCount = 1};
            ranges = &one;
            c.buildAccelerationStructuresKHR(1, &build, &ranges);
            Barrier(c, vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eHost,
                    vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eHostWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
            const vk::WriteDescriptorSetAccelerationStructureKHR as_info{
                .accelerationStructureCount = 1, .pAccelerationStructures = &as_t};
            const std::array image_infos{
                vk::DescriptorImageInfo{{}, material, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{{}, depth, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{{}, *owned->raw.view, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{{}, scene, vk::ImageLayout::eGeneral}};
            const std::array writes{
                vk::WriteDescriptorSet{.pNext = &as_info,
                                       .dstBinding = 0,
                                       .descriptorCount = 1,
                                       .descriptorType =
                                           vk::DescriptorType::eAccelerationStructureKHR},
                vk::WriteDescriptorSet{.dstBinding = 1,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eStorageBuffer,
                                       .pBufferInfo = &vertex_info},
                vk::WriteDescriptorSet{.dstBinding = 2,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eStorageBuffer,
                                       .pBufferInfo = &index_info},
                vk::WriteDescriptorSet{.dstBinding = 3,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eStorageBuffer,
                                       .pBufferInfo = &frame_info},
                vk::WriteDescriptorSet{.dstBinding = 4,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &image_infos[0]},
                vk::WriteDescriptorSet{.dstBinding = 5,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &image_infos[1]},
                vk::WriteDescriptorSet{.dstBinding = 6,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eStorageImage,
                                       .pImageInfo = &image_infos[2]},
                vk::WriteDescriptorSet{.dstBinding = 7,
                                       .descriptorCount = 1,
                                       .descriptorType = vk::DescriptorType::eSampledImage,
                                       .pImageInfo = &image_infos[3]}};
            c.bindPipeline(vk::PipelineBindPoint::eCompute, trace);
            c.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, tl, 0, writes);
            c.dispatch((w + 7) / 8, (h + 7) / 8, 1);
            Barrier(c, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderWrite, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
            const std::array infos{
                vk::DescriptorImageInfo{{}, *owned->raw.view, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{{}, depth, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{{}, *owned->motion.view, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{
                    {}, *owned->history[history ^ 1].view, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{
                    {}, *owned->history[history].view, vk::ImageLayout::eGeneral},
                vk::DescriptorImageInfo{{}, scene, vk::ImageLayout::eGeneral}};
            std::array<vk::WriteDescriptorSet, 7> dw{};
            for (u32 n = 0; n < 6; n++)
                dw[n] = {.dstBinding = n,
                         .descriptorCount = 1,
                         .descriptorType = n < 4 ? vk::DescriptorType::eSampledImage
                                                 : vk::DescriptorType::eStorageImage,
                         .pImageInfo = &infos[n]};
            dw[6] = {.dstBinding = 6,
                     .descriptorCount = 1,
                     .descriptorType = vk::DescriptorType::eStorageBuffer,
                     .pBufferInfo = &frame_info};
            c.bindPipeline(vk::PipelineBindPoint::eCompute, denoise);
            c.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, dl, 0, dw);
            c.dispatch((w + 7) / 8, (h + 7) / 8, 1);
            Barrier(c, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderWrite, vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        });
        ticks[frame_slot] = scheduler.CurrentTick();
        previous_position = cam.position;
        rendered = true;
        waiting_logged = false;
        last_render_frame = frame_id;
        if (++render_count % 120 == 0 || render_count < 3)
            std::printf("RTX path tracing active: %ux%u, %u draws / %u triangles / %u skipped, %u "
                        "spp, %u bounces, %s\n",
                        w, h, draws, primitives, skipped, frame.options[2], frame.options[3],
                        mode == 1 ? "path-traced lighting" : "hybrid indirect");
        return true;
    }
};

PathTracer::PathTracer(const Instance &i, Scheduler &s) : impl(std::make_unique<Impl>(i, s)) {
    if (Enabled())
        BeginFrame();
}
PathTracer::PathTracer(const Instance &i, Scheduler &s, CameraMotion &c, VideoCore::TextureCache &t,
                       Runtime &r, SceneTargets &st)
    : PathTracer(i, s) {
    impl->camera = &c;
    impl->textures = &t;
    impl->runtime = &r;
    impl->targets = &st;
}
PathTracer::~PathTracer() = default;
bool PathTracer::Enabled() const { return impl->enabled; }
void PathTracer::BeginFrame() { impl->Begin(); }
u32 PathTracer::Capture(std::span<const u16> x, u32 b, u32 n, u32 f) {
    return impl->Capture(x, b, n, f);
}
u32 PathTracer::Capture(std::span<const u32> x, u32 b, u32 n, u32 f) {
    return impl->Capture(x, b, n, f);
}
u32 PathTracer::CaptureNonIndexed(u32 count, u32 b, u32 n, u32 f) {
    if (count > impl->max_vertices || count % 3)
        return 0;
    std::vector<u32> x(count);
    for (u32 i = 0; i < count; i++)
        x[i] = i;
    return Capture(std::span<const u32>{x}, b, n, f);
}
void PathTracer::SetAlbedo(VideoCore::ImageId color, VideoCore::ImageId depth) {
    if (Enabled()) {
        impl->albedo = color;
        impl->albedo_depth = depth;
    }
}
bool PathTracer::Render(VideoCore::ImageId scene) {
    auto &p = *impl;
    if (!Enabled() || !p.camera || !scene || !p.albedo || p.albedo_depth != p.camera->Depth())
        return false;
    auto camera = p.camera->RayCamera();
    if (!RayGeometry::Valid(camera))
        return false;
    auto &color = p.textures->GetImage(scene);
    auto &depth = p.textures->GetImage(p.camera->Depth());
    auto &albedo = p.textures->GetImage(p.albedo);
    if (color.info.pixel_format != vk::Format::eR16G16B16A16Sfloat ||
        !(color.usage_flags & vk::ImageUsageFlagBits::eStorage) ||
        color.info.size.width != depth.info.size.width ||
        color.info.size.height != depth.info.size.height)
        return false;
    p.scheduler.EndRendering();
    const auto read = vk::AccessFlagBits2::eShaderRead;
    auto view = [&](VideoCore::Image &image, bool is_depth, bool storage = false) {
        VideoCore::ImageViewInfo info;
        info.format = is_depth ? vk::Format::eR32Sfloat : image.info.pixel_format;
        info.is_storage = storage;
        info.type = AmdGpu::ImageType::Color2D;
        return *image.FindView(info).image_view;
    };
    vk::ImageView c, d, a;
    u32 w = color.info.size.width, h = color.info.size.height;
    if (p.targets->Reduced() && p.targets->EligibleScene(color) &&
        p.targets->EligibleScene(depth) && p.targets->EligibleScene(albedo)) {
        VideoCore::ImageViewInfo info;
        info.format = color.info.pixel_format;
        info.is_storage = true;
        c = p.targets
                ->Read(scene, info, vk::PipelineStageFlagBits2::eComputeShader,
                       read | vk::AccessFlagBits2::eShaderWrite)
                .view;
        info.is_storage = false;
        info.format = vk::Format::eR32Sfloat;
        info.type = AmdGpu::ImageType::Color2D;
        d = p.targets->Read(p.camera->Depth(), info).view;
        info.type = AmdGpu::ImageType::Color2D;
        info.format = albedo.info.pixel_format;
        a = p.targets->Read(p.albedo, info).view;
        w = p.targets->Size().width;
        h = p.targets->Size().height;
    } else {
        for (auto *image : {&color, &depth, &albedo})
            p.runtime->Transit(image, vk::ImageLayout::eGeneral,
                               vk::PipelineStageFlagBits2::eComputeShader,
                               image == &color ? read | vk::AccessFlagBits2::eShaderWrite : read);
        p.runtime->FlushBarriers();
        c = view(color, false, true);
        d = view(depth, true);
        a = view(albedo, false);
    }
    return p.Draw(camera, c, a, d, w, h);
}
bool PathTracer::RenderImages(const RayGeometry::Camera &c, vk::ImageView s, vk::ImageView a,
                              vk::ImageView d, u32 w, u32 h) {
    return impl->Draw(c, s, a, d, w, h);
}
void PathTracer::TestScene(std::span<const std::array<float, 4>> vertices,
                           std::span<const u32> indices) {
    if (!Enabled() || vertices.size() > impl->max_vertices)
        return;
    for (const auto &v : vertices)
        for (float x : v)
            if (!std::isfinite(x))
                return;
    std::vector<u32> checked;
    if (!RayGeometry::Indices(indices, 0, u32(vertices.size()), 0, 1, checked, impl->max_indices))
        return;
    auto upload = std::make_shared<Buffer>();
    if (!upload->Create(impl->instance, vertices.size_bytes(),
                        vk::BufferUsageFlagBits::eTransferSrc, true))
        return;
    std::memcpy(upload->mapped, vertices.data(), vertices.size_bytes());
    impl->scheduler.Record([upload, dest = impl->positions.handle](vk::CommandBuffer c) {
        Barrier(c, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        c.copyBuffer(upload->handle, dest, vk::BufferCopy{0, 0, upload->size});
    });
    impl->scheduler.DeferOperation([upload] {});
    impl->used_vertices = u32(vertices.size());
    impl->index_data = std::move(checked);
    impl->draws = 1;
}
} // namespace Vulkan
