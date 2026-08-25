#include <graphics/vulkan/SkinnedPosePass.h>

#include <graphics/FrameScratchRing.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <assets/static_mesh/StaticMeshVertex.h>
#include <shaders/kSkinPoseCompSpv.h>

namespace
{
// Must match skin_pose.comp.glsl's local_size_x.
constexpr std::uint32_t kWorkGroupSize = 64;

struct SkinPosePush
{
    std::uint32_t VertexCount = 0;
};
} // namespace

void SkinnedPosePass::Setup(const RendererServices& services)
{
    Buffers = services.Buffers;
    Pipelines = services.Pipelines;
    Shaders = services.Shaders;
    Log = services.Logging != nullptr
        ? &services.Logging->GetLogger<SkinnedPosePass>() : nullptr;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    if (services.PhysicalDevice != nullptr)
    {
        StorageOffsetAlignment =
            services.PhysicalDevice->GetProperties().limits.minStorageBufferOffsetAlignment;
        if (StorageOffsetAlignment == 0)
            StorageOffsetAlignment = 1;
        if (Log != nullptr && StorageOffsetAlignment > kMaxDescriptorOffsetAlignment)
        {
            // Vulkan caps this limit at 256, which is what the palette packing
            // upstream aligns to without querying anything.
            Log->Error("minStorageBufferOffsetAlignment is {}, above the {} byte "
                       "ceiling palette packing assumes; poses will be skipped",
                       StorageOffsetAlignment, kMaxDescriptorOffsetAlignment);
        }
    }
    if (Device == VK_NULL_HANDLE || Shaders == nullptr)
        return;

    ComputeShader = Shaders->CreateModuleFromSpirv(
        kSkinPoseCompSpv, kSkinPoseCompSpvWordCount, "Skin pose compute");

    // Four storage bindings: rest vertices, influences, palette, posed out.
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (std::uint32_t binding = 0; binding < 4; ++binding)
    {
        bindings[binding].binding = binding;
        bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[binding].descriptorCount = 1;
        bindings[binding].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(Device, &layoutInfo, nullptr, &SetLayout)
        != VK_SUCCESS)
    {
        SetLayout = VK_NULL_HANDLE;
        return;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.size = sizeof(SkinPosePush);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &SetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(Device, &pipelineLayoutInfo, nullptr, &PipelineLayout)
        != VK_SUCCESS)
    {
        PipelineLayout = VK_NULL_HANDLE;
        return;
    }

    // One pool per frame in flight, reset each frame before its jobs
    // allocate; a set lives exactly as long as its slot's recording.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = kMaxDispatchesPerFrame * 4;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kMaxDispatchesPerFrame;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    for (VkDescriptorPool& pool : Pools)
    {
        if (vkCreateDescriptorPool(Device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
            pool = VK_NULL_HANDLE;
    }
}

BufferHandle SkinnedPosePass::CreatePosedBuffer(std::uint32_t vertexCount)
{
    if (Buffers == nullptr || vertexCount == 0)
        return {};
    return Buffers->Create(BufferCreateInfo{
        .Size = static_cast<VkDeviceSize>(vertexCount) * sizeof(StaticMeshVertex),
        .Usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
               | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .Memory = BufferMemory::GpuOnly,
        .DebugName = "Posed skinned vertices",
    });
}

void SkinnedPosePass::DestroyPosedBuffer(BufferHandle buffer)
{
    if (Buffers != nullptr && buffer.IsValid())
        Buffers->Destroy(buffer);
}

std::uint32_t SkinnedPosePass::Record(const FrameContext& frame,
                                      std::span<const SkinnedPoseDispatch> dispatches)
{
    if (dispatches.empty() || Pipelines == nullptr || Buffers == nullptr
        || PipelineLayout == VK_NULL_HANDLE
        || frame.FrameInFlightIndex >= Pools.size())
    {
        return 0;
    }
    VkDescriptorPool pool = Pools[frame.FrameInFlightIndex];
    if (pool == VK_NULL_HANDLE)
        return 0;

    const VkPipeline pipeline = Pipelines->GetComputePipeline(
        ComputePipelineDesc{ ComputeShader, PipelineLayout });
    if (pipeline == VK_NULL_HANDLE)
        return 0;

    vkResetDescriptorPool(Device, pool, 0);
    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    std::uint32_t recorded = 0;
    for (const SkinnedPoseDispatch& dispatch : dispatches)
    {
        if (recorded >= kMaxDispatchesPerFrame)
            break;
        const VkBuffer rest = Buffers->GetBuffer(dispatch.RestVertices);
        const VkBuffer influences = Buffers->GetBuffer(dispatch.Influences);
        const VkBuffer posed = Buffers->GetBuffer(dispatch.PosedVertices);
        const VkBuffer palette = Buffers->GetBuffer(dispatch.PaletteBuffer);
        if (rest == VK_NULL_HANDLE || influences == VK_NULL_HANDLE
            || posed == VK_NULL_HANDLE || palette == VK_NULL_HANDLE
            || dispatch.VertexCount == 0)
        {
            continue;
        }
        if (dispatch.PaletteOffset % StorageOffsetAlignment != 0)
        {
            // Writing this into a descriptor would be undefined behavior the
            // validation layers report as VUID-VkWriteDescriptorSet-descriptorType-00327.
            if (Log != nullptr)
            {
                Log->Error("Palette offset {} is not a multiple of the device's {} byte "
                           "storage alignment; skipping the dispatch",
                           dispatch.PaletteOffset, StorageOffsetAlignment);
            }
            continue;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &SetLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(Device, &allocInfo, &set) != VK_SUCCESS)
            break;

        const VkDescriptorBufferInfo buffers[4] = {
            { rest, 0, VK_WHOLE_SIZE },
            { influences, 0, VK_WHOLE_SIZE },
            { palette, dispatch.PaletteOffset, dispatch.PaletteBytes },
            { posed, 0, VK_WHOLE_SIZE },
        };
        VkWriteDescriptorSet writes[4]{};
        for (std::uint32_t binding = 0; binding < 4; ++binding)
        {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = set;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[binding].pBufferInfo = &buffers[binding];
        }
        vkUpdateDescriptorSets(Device, 4, writes, 0, nullptr);

        vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                PipelineLayout, 0, 1, &set, 0, nullptr);
        const SkinPosePush push{ dispatch.VertexCount };
        vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(push), &push);
        vkCmdDispatch(frame.Cmd,
                      (dispatch.VertexCount + kWorkGroupSize - 1) / kWorkGroupSize,
                      1, 1);
        ++recorded;
    }

    if (recorded > 0)
    {
        // The dispatches' storage writes become visible to this frame's
        // vertex fetches of the posed buffers. Cross-frame reuse of a slot's
        // buffer needs nothing extra: the frame service's fence wait already
        // orders it.
        VkMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(frame.Cmd, &dependency);
    }
    return recorded;
}

void SkinnedPosePass::Teardown()
{
    for (VkDescriptorPool& pool : Pools)
    {
        if (pool != VK_NULL_HANDLE && Device != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(Device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    }
    if (PipelineLayout != VK_NULL_HANDLE && Device != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
    PipelineLayout = VK_NULL_HANDLE;
    if (SetLayout != VK_NULL_HANDLE && Device != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
    SetLayout = VK_NULL_HANDLE;
    if (Shaders != nullptr)
        Shaders->Destroy(ComputeShader);
    ComputeShader = {};
    Pipelines = nullptr;
    Shaders = nullptr;
    Device = VK_NULL_HANDLE;
}
