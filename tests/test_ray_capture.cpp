// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the actual recompiled vertex epilogue, including packed motion/ray indices.
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "video_core/renderer_vulkan/ray_geometry.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <vk_mem_alloc.h>
// Keep test_assert last: standard/Vulkan headers include assert.h again.
#include "test_assert.h"

using namespace Vulkan;
int main() {
    const std::array<float, 12> view{0, 0, 1, -30, 0, 1, 0, -20, -1, 0, 0, 10};
    const std::array<float, 12> inverse{0, 0, -1, 10, 0, 1, 0, 20, 1, 0, 0, 30};
    const std::array<float, 4> raw{1.7f, 2, 1.00002f, -0.05f};
    for (float flip : {1.f, -1.f}) {
        auto screen = raw;
        screen[1] *= flip;
        auto c = RayGeometry::MakeCamera(view, inverse, raw, screen, true);
        assert(RayGeometry::Valid(c));
        const std::array<float, 4> world{8, 21, 32, 1};
        std::array<float, 4> clip{}, restored{};
        for (u32 r = 0; r < 4; r++)
            for (u32 k = 0; k < 4; k++)
                clip[r] += c.world_to_clip[k * 4 + r] * world[k];
        for (u32 r = 0; r < 4; r++)
            for (u32 k = 0; k < 4; k++)
                restored[r] += c.clip_to_world[k * 4 + r] * clip[k];
        for (u32 r = 0; r < 4; r++)
            assert(std::abs(restored[r] - world[r]) < 0.001f);
    }
    std::vector<u32> converted;
    const std::array<u16, 3> source{10, 12, 11};
    assert(RayGeometry::Indices(std::span<const u16>(source), 10, 3, 20, 2, converted, 6));
    assert((converted == std::vector<u32>{20, 22, 21, 23, 25, 24}));
    const auto previous = converted;
    assert(!RayGeometry::Indices(std::span<const u16>(source), 11, 3, 20, 1, converted, 20));
    assert(!RayGeometry::Indices(std::span<const u16>(source), 10, 3, UINT32_MAX - 1, 1, converted,
                                 20));
    assert(converted == previous);
    Instance instance(0, false);
    Scheduler scheduler(instance);
    const auto device = instance.GetDevice();
    struct HostBuffer {
        VkBuffer handle{};
        VmaAllocation allocation{};
        VmaAllocationInfo info{};
    };
    const auto allocate = [&](u64 size) {
        HostBuffer b;
        const VkBufferCreateInfo ci{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                    .size = size,
                                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
        const VmaAllocationCreateInfo ai{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                                  VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                                         .usage = VMA_MEMORY_USAGE_AUTO,
                                         .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        assert(vmaCreateBuffer(instance.GetAllocator(), &ci, &ai, &b.handle, &b.allocation,
                               &b.info) == VK_SUCCESS);
        return b;
    };
    auto params = allocate(192), output = allocate(16 * 16), indices = allocate(16);
    const std::array<u16, 3> indexed{0, 1, 2};
    std::memcpy(indices.info.pMappedData, indexed.data(), 6);
    std::memset(params.info.pMappedData, 0, 192);
    std::memset(output.info.pMappedData, 0, 16 * 16);
    auto *p = static_cast<RayGeometry::CaptureParams *>(params.info.pMappedData);
    p[1] = {.range = {4, 3, 7, 3},
            .instances = {2, 0, 0, 0},
            .inverse_view = {1, 0, 0, 10, 0, 1, 0, 20, 0, 0, 1, 30},
            .inverse_projection = {0.5f, 0.25f, 0, 0}};
    using namespace Shader;
    Info info{};
    info.hw_stage = HwStage::Vertex;
    info.sw_stage = SwStage::Vertex;
    RuntimeInfo runtime{};
    runtime.Initialize(info.hw_stage, info.sw_stage);
    runtime.hw.vs.ray_params_address = device.getBufferAddress({.buffer = params.handle});
    runtime.hw.vs.ray_positions_address = device.getBufferAddress({.buffer = output.handle});
    Common::ObjectPool<IR::Inst> pool;
    IR::Block block(pool);
    IR::IREmitter ir(block);
    const std::array<float, 4> clip{4, 6, 1, 2};
    for (u32 n = 0; n < 4; n++) {
        info.stores.Set(IR::Attribute::Position0, n);
        ir.SetAttribute(IR::Attribute::Position0, ir.Imm32(clip[n]), n);
    }
    ir.Epilogue();
    IR::Program program(info);
    program.blocks.push_back(&block);
    program.syntax_list.push_back(
        {.data = {.block = &block}, .type = IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
    Profile profile{};
    profile.supported_spirv = 0x00010600;
    profile.support_int64 = true;
    Backend::Bindings bindings{};
    const auto code = Backend::SPIRV::EmitSPIRV(profile, runtime, program, bindings);
    auto module =
        device.createShaderModuleUnique({.codeSize = code.size() * 4, .pCode = code.data()}).value;
    const vk::PushConstantRange push{vk::ShaderStageFlagBits::eVertex, 0, 128};
    auto layout =
        device
            .createPipelineLayoutUnique({.pushConstantRangeCount = 1, .pPushConstantRanges = &push})
            .value;
    const vk::PipelineShaderStageCreateInfo stage{
        .stage = vk::ShaderStageFlagBits::eVertex, .module = *module, .pName = "main"};
    const vk::PipelineVertexInputStateCreateInfo vertex{};
    const vk::PipelineInputAssemblyStateCreateInfo assembly{
        .topology = vk::PrimitiveTopology::eTriangleList};
    const vk::Viewport viewport{0, 0, 1, 1, 0, 1};
    const vk::Rect2D scissor{{0, 0}, {1, 1}};
    const vk::PipelineViewportStateCreateInfo vp{
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    const vk::PipelineRasterizationStateCreateInfo raster{.rasterizerDiscardEnable = true,
                                                          .lineWidth = 1};
    const vk::PipelineRenderingCreateInfo render{};
    auto result = device.createGraphicsPipelineUnique({}, {.pNext = &render,
                                                           .stageCount = 1,
                                                           .pStages = &stage,
                                                           .pVertexInputState = &vertex,
                                                           .pInputAssemblyState = &assembly,
                                                           .pViewportState = &vp,
                                                           .pRasterizationState = &raster,
                                                           .layout = *layout});
    assert(result.result == vk::Result::eSuccess);
    auto pipeline = std::move(result.value);
    const auto run = [&](u32 param, u32 first, u32 count, u32 first_instance, u32 instances,
                         bool negative = false) {
        scheduler.Record([=, &layout, &pipeline](vk::CommandBuffer cmd) {
            const vk::MemoryBarrier2 host{.srcStageMask = vk::PipelineStageFlagBits2::eHost,
                                          .srcAccessMask = vk::AccessFlagBits2::eHostWrite,
                                          .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
                                          .dstAccessMask = vk::AccessFlagBits2::eShaderRead |
                                                           vk::AccessFlagBits2::eShaderWrite};
            cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &host});
            cmd.beginRendering({.renderArea = {{0, 0}, {1, 1}}, .layerCount = 1});
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
            PushData data{};
            data.motion_param = param;
            cmd.pushConstants(*layout, vk::ShaderStageFlagBits::eVertex, 0, 128, &data);
            if (negative) {
                cmd.bindIndexBuffer(indices.handle, 0, vk::IndexType::eUint16);
                cmd.drawIndexed(count, instances, 0, -3, first_instance);
            } else
                cmd.draw(count, instances, first, first_instance);
            cmd.endRendering();
            const vk::MemoryBarrier2 read{.srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
                                          .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
                                          .dstStageMask = vk::PipelineStageFlagBits2::eHost,
                                          .dstAccessMask = vk::AccessFlagBits2::eHostRead};
            cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &read});
        });
        scheduler.Finish();
    };
    run((1u << 16) | 0xf0f0u, 7, 3, 3, 2);
    auto *values = static_cast<std::array<float, 4> *>(output.info.pMappedData);
    for (u32 n = 0; n < 16; n++)
        for (u32 x = 0; x < 4; x++) {
            const std::array<float, 4> expected{12, 21.5f, 32, 1};
            assert(values[n][x] == (n >= 4 && n < 10 ? expected[x] : 0));
        }
    std::memset(output.info.pMappedData, 0, 256);
    run(0xffffu, 7, 3, 3, 2);      // low half is not a ray parameter; no capture
    run(1u << 16, 100, 3, 100, 2); // out-of-range draw cannot write the buffer
    for (u32 n = 0; n < 16; n++)
        for (float x : values[n])
            assert(x == 0);
    p[1].range[2] = u32(-3);
    run(1u << 16, 0, 3, 3, 2, true);
    for (u32 n = 4; n < 10; n++)
        assert(values[n][0] == 12 && values[n][1] == 21.5f && values[n][2] == 32 &&
               values[n][3] == 1);
    vmaDestroyBuffer(instance.GetAllocator(), params.handle, params.allocation);
    vmaDestroyBuffer(instance.GetAllocator(), output.handle, output.allocation);
    vmaDestroyBuffer(instance.GetAllocator(), indices.handle, indices.allocation);
    std::puts("RTX recompiled vertex capture: PASS");
}
