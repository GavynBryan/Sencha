#include <render/pass/LightBindings.h>

#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanUploadContextService.h>

namespace
{
    VkSamplerCreateInfo MakeClampedLinearSampler()
    {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.minLod = 0.0f;
        info.maxLod = 0.0f;
        return info;
    }
}

bool LightBindings::Setup(const RendererServices& services)
{
    Images = services.Images;
    Upload = services.Upload;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    if (Images == nullptr || Device == VK_NULL_HANDLE || Upload == nullptr)
        return false;

    if (!CreateSamplers() || !CreateDummies() || !CreateSetObjects())
    {
        Teardown();
        return false;
    }

    WriteBinding(0, 0, ShadowSampler, Images->GetView(DummyShadowMap),
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    WriteBinding(1, 0, PointShadowSampler, Images->GetView(DummyShadowCube),
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    WriteBinding(3, 0, RawShadowSampler, Images->GetView(DummyShadowMap),
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    WriteBinding(4, 0, RawPointShadowSampler, Images->GetView(DummyShadowCube),
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
#endif
    for (std::uint32_t element = 0;
         element < kMaxActiveProbeVolumes * kProbeVolumeChannelCount; ++element)
    {
        WriteBinding(2, element, ProbeSampler, Images->GetView(DummyProbeVolume),
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return true;
}

void LightBindings::SetProbeVolume(std::uint32_t slot, ImageHandle r,
                                   ImageHandle g, ImageHandle b)
{
    if (slot >= kMaxActiveProbeVolumes || !IsValid())
        return;
    const std::uint32_t base = slot * kProbeVolumeChannelCount;
    const VkImageView views[kProbeVolumeChannelCount] = {
        Images->GetView(r), Images->GetView(g), Images->GetView(b) };
    for (std::uint32_t channel = 0; channel < kProbeVolumeChannelCount; ++channel)
    {
        WriteBinding(2, base + channel, ProbeSampler, views[channel],
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
}

void LightBindings::ResetProbeVolume(std::uint32_t slot)
{
    if (slot >= kMaxActiveProbeVolumes || !IsValid())
        return;
    SetProbeVolume(slot, DummyProbeVolume, DummyProbeVolume, DummyProbeVolume);
}

bool LightBindings::CreateSamplers()
{
    VkSamplerCreateInfo shadowInfo = MakeClampedLinearSampler();
    shadowInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    shadowInfo.compareEnable = VK_TRUE;
    shadowInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    shadowInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(Device, &shadowInfo, nullptr, &ShadowSampler) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo pointShadowInfo = MakeClampedLinearSampler();
    pointShadowInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pointShadowInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pointShadowInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    pointShadowInfo.compareEnable = VK_TRUE;
    pointShadowInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (vkCreateSampler(Device, &pointShadowInfo, nullptr, &PointShadowSampler)
        != VK_SUCCESS)
    {
        return false;
    }

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    VkSamplerCreateInfo rawShadowInfo = shadowInfo;
    rawShadowInfo.magFilter = VK_FILTER_NEAREST;
    rawShadowInfo.minFilter = VK_FILTER_NEAREST;
    rawShadowInfo.compareEnable = VK_FALSE;
    if (vkCreateSampler(Device, &rawShadowInfo, nullptr, &RawShadowSampler)
        != VK_SUCCESS)
    {
        return false;
    }

    VkSamplerCreateInfo rawPointShadowInfo = pointShadowInfo;
    rawPointShadowInfo.magFilter = VK_FILTER_NEAREST;
    rawPointShadowInfo.minFilter = VK_FILTER_NEAREST;
    rawPointShadowInfo.compareEnable = VK_FALSE;
    if (vkCreateSampler(Device, &rawPointShadowInfo, nullptr,
                        &RawPointShadowSampler) != VK_SUCCESS)
    {
        return false;
    }
#endif

    VkSamplerCreateInfo probeInfo = MakeClampedLinearSampler();
    probeInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    probeInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    probeInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return vkCreateSampler(Device, &probeInfo, nullptr, &ProbeSampler) == VK_SUCCESS;
}

bool LightBindings::CreateDummies()
{
    DummyShadowMap = Images->Create(ImageCreateInfo{
        .Format = VK_FORMAT_D16_UNORM,
        .Extent = { 1, 1 },
        .Usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .DebugName = "Dummy shadow map",
    });
    DummyShadowCube = Images->Create(ImageCreateInfo{
        .Format = VK_FORMAT_D16_UNORM,
        .Extent = { 1, 1 },
        .Usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .ViewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
        .ArrayLayers = 6,
        .DebugName = "Dummy shadow cube array",
    });
    DummyProbeVolume = Images->Create(ImageCreateInfo{
        .Format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .Extent = { 1, 1 },
        .Usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .ViewType = VK_IMAGE_VIEW_TYPE_3D,
        .Depth = 1,
        .DebugName = "Dummy probe volume",
    });
    if (!DummyShadowMap.IsValid() || !DummyShadowCube.IsValid()
        || !DummyProbeVolume.IsValid())
    {
        return false;
    }

    VkCommandBuffer cmd = Upload->Begin();
    if (cmd == VK_NULL_HANDLE)
        return false;

    const auto transition = [&](ImageHandle image, VkImageAspectFlags aspect,
                                std::uint32_t layers, VkImageLayout oldLayout,
                                VkImageLayout newLayout, bool toClear)
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = Images->GetImage(image);
        t.OldLayout = oldLayout;
        t.NewLayout = newLayout;
        t.SrcStage = toClear ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                             : VK_PIPELINE_STAGE_2_CLEAR_BIT;
        t.DstStage = toClear ? VK_PIPELINE_STAGE_2_CLEAR_BIT
                             : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        t.SrcAccess = toClear ? 0 : VK_ACCESS_2_TRANSFER_WRITE_BIT;
        t.DstAccess = toClear ? VK_ACCESS_2_TRANSFER_WRITE_BIT
                              : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        t.AspectMask = aspect;
        t.LayerCount = layers;
        VulkanBarriers::TransitionImage(cmd, t);
    };

    VkImageSubresourceRange depthRange{};
    depthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthRange.levelCount = 1;
    depthRange.layerCount = 1;
    VkClearDepthStencilValue depthClear{ 1.0f, 0 };

    transition(DummyShadowMap, VK_IMAGE_ASPECT_DEPTH_BIT, 1,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    vkCmdClearDepthStencilImage(cmd, Images->GetImage(DummyShadowMap),
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                &depthClear, 1, &depthRange);
    transition(DummyShadowMap, VK_IMAGE_ASPECT_DEPTH_BIT, 1,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);

    depthRange.layerCount = 6;
    transition(DummyShadowCube, VK_IMAGE_ASPECT_DEPTH_BIT, 6,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    vkCmdClearDepthStencilImage(cmd, Images->GetImage(DummyShadowCube),
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                &depthClear, 1, &depthRange);
    transition(DummyShadowCube, VK_IMAGE_ASPECT_DEPTH_BIT, 6,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);

    VkImageSubresourceRange colorRange{};
    colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorRange.levelCount = 1;
    colorRange.layerCount = 1;
    VkClearColorValue colorClear{};

    transition(DummyProbeVolume, VK_IMAGE_ASPECT_COLOR_BIT, 1,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true);
    vkCmdClearColorImage(cmd, Images->GetImage(DummyProbeVolume),
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &colorClear, 1, &colorRange);
    transition(DummyProbeVolume, VK_IMAGE_ASPECT_COLOR_BIT, 1,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);

    return Upload->Submit(cmd);
}

bool LightBindings::CreateSetObjects()
{
    const VkDescriptorSetLayoutBinding bindings[] = {
        { .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr },
        { .binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr },
        { .binding = 2,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = kMaxActiveProbeVolumes * kProbeVolumeChannelCount,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr },
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        { .binding = 3,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr },
        { .binding = 4,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .pImmutableSamplers = nullptr },
#endif
    };

    // Binding 2 swaps probe volumes in and out while frames holding this set
    // are still in flight, so it is update-after-bind, the same contract the
    // bindless material set relies on. It is not partially bound: every
    // element must name a live view, which is why a vacated slot points at the
    // dummy rather than at nothing. The shadow bindings stay
    // write-once-at-setup.
    constexpr std::uint32_t bindingCount = sizeof(bindings) / sizeof(bindings[0]);
    VkDescriptorBindingFlags bindingFlags[bindingCount] = {};
    bindingFlags[2] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = bindingCount;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = bindingCount;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(Device, &layoutInfo, nullptr, &SetLayout) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 2 + kMaxActiveProbeVolumes * kProbeVolumeChannelCount;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    poolSize.descriptorCount += 2;
#endif

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(Device, &poolInfo, nullptr, &Pool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = Pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &SetLayout;
    return vkAllocateDescriptorSets(Device, &allocateInfo, &Set) == VK_SUCCESS;
}

void LightBindings::WriteBinding(std::uint32_t binding, std::uint32_t arrayElement,
                                 VkSampler sampler, VkImageView view,
                                 VkImageLayout layout)
{
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = view;
    imageInfo.imageLayout = layout;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = Set;
    write.dstBinding = binding;
    write.dstArrayElement = arrayElement;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);
}

// Parks a fresh depth target in the sampled layout so its descriptor stays
// valid to bind even on frames that render no shadow views (zero shadowed
// lights, or viewports with no shadow pass at all). Cleared to the far plane
// on the way, matching the dummy images: a tile that no view has rendered
// into yet is sampled on those frames, and undefined image memory reads
// differently on different drivers.
bool LightBindings::ParkDepthImage(VkImage image, std::uint32_t layerCount)
{
    VkCommandBuffer cmd = Upload->Begin();
    if (cmd == VK_NULL_HANDLE)
        return false;

    VulkanBarriers::ImageTransition transition{};
    transition.Image = image;
    transition.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    transition.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition.SrcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    transition.SrcAccess = 0;
    transition.DstAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    transition.LayerCount = layerCount;
    VulkanBarriers::TransitionImage(cmd, transition);

    VkImageSubresourceRange depthRange{};
    depthRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthRange.levelCount = 1;
    depthRange.layerCount = layerCount;
    const VkClearDepthStencilValue depthClear{ 1.0f, 0 };
    vkCmdClearDepthStencilImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                &depthClear, 1, &depthRange);

    transition.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    transition.SrcStage = VK_PIPELINE_STAGE_2_CLEAR_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    transition.SrcAccess = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    transition.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    VulkanBarriers::TransitionImage(cmd, transition);

    return Upload->Submit(cmd);
}

bool LightBindings::CreateAtlas()
{
    if (!IsValid())
        return false;
    if (Atlas.IsValid())
        return true;

    Atlas = Images->Create(ImageCreateInfo{
        .Format = VK_FORMAT_D16_UNORM,
        .Extent = { kSpotShadowAtlasExtent, kSpotShadowAtlasExtent },
        .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .DebugName = "Spot shadow atlas",
    });
    if (!Atlas.IsValid())
        return false;

    if (!ParkDepthImage(GetAtlasImage(), 1))
    {
        Images->Destroy(Atlas);
        Atlas = {};
        return false;
    }
    AtlasLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    WriteBinding(0, 0, ShadowSampler, Images->GetView(Atlas),
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    WriteBinding(3, 0, RawShadowSampler, Images->GetView(Atlas),
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
#endif
    return true;
}

bool LightBindings::CreateCubePool()
{
    if (!IsValid())
        return false;
    if (CubePool.IsValid())
        return true;

    constexpr std::uint32_t layerCount = kMaxPointShadows * kPointShadowFaceCount;
    CubePool = Images->Create(ImageCreateInfo{
        .Format = VK_FORMAT_D16_UNORM,
        .Extent = { kPointShadowFaceExtent, kPointShadowFaceExtent },
        .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .ViewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY,
        .ArrayLayers = layerCount,
        .DebugName = "Point shadow cube pool",
    });
    if (!CubePool.IsValid())
        return false;

    const auto destroyPool = [this]
    {
        DestroyCubeFaceViews();
        Images->Destroy(CubePool);
        CubePool = {};
    };

    for (std::uint32_t layer = 0; layer < layerCount; ++layer)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = Images->GetImage(CubePool);
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_D16_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = layer;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(Device, &viewInfo, nullptr, &CubeFaceViews[layer])
            != VK_SUCCESS)
        {
            destroyPool();
            return false;
        }
    }

    if (!ParkDepthImage(GetCubePoolImage(), layerCount))
    {
        destroyPool();
        return false;
    }
    CubeLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    WriteBinding(1, 0, PointShadowSampler, Images->GetView(CubePool),
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    WriteBinding(4, 0, RawPointShadowSampler, Images->GetView(CubePool),
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
#endif
    return true;
}

void LightBindings::DestroyCubeFaceViews()
{
    for (VkImageView& view : CubeFaceViews)
    {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(Device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
}

void LightBindings::Teardown()
{
    Set = VK_NULL_HANDLE;
    if (Pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Pool, nullptr);
    Pool = VK_NULL_HANDLE;
    if (SetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
    SetLayout = VK_NULL_HANDLE;
    if (ShadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, ShadowSampler, nullptr);
    ShadowSampler = VK_NULL_HANDLE;
    if (PointShadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, PointShadowSampler, nullptr);
    PointShadowSampler = VK_NULL_HANDLE;
    if (ProbeSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, ProbeSampler, nullptr);
    ProbeSampler = VK_NULL_HANDLE;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (RawShadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, RawShadowSampler, nullptr);
    RawShadowSampler = VK_NULL_HANDLE;
    if (RawPointShadowSampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, RawPointShadowSampler, nullptr);
    RawPointShadowSampler = VK_NULL_HANDLE;
#endif
    DestroyCubeFaceViews();
    if (Images != nullptr)
    {
        if (Atlas.IsValid())
            Images->Destroy(Atlas);
        if (CubePool.IsValid())
            Images->Destroy(CubePool);
        if (DummyShadowMap.IsValid())
            Images->Destroy(DummyShadowMap);
        if (DummyShadowCube.IsValid())
            Images->Destroy(DummyShadowCube);
        if (DummyProbeVolume.IsValid())
            Images->Destroy(DummyProbeVolume);
    }
    Atlas = {};
    CubePool = {};
    DummyShadowMap = {};
    DummyShadowCube = {};
    DummyProbeVolume = {};
    AtlasLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    CubeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Images = nullptr;
    Upload = nullptr;
    Device = VK_NULL_HANDLE;
}

bool LightBindings::IsValid() const
{
    return SetLayout != VK_NULL_HANDLE
        && Set != VK_NULL_HANDLE
        && ShadowSampler != VK_NULL_HANDLE
        && PointShadowSampler != VK_NULL_HANDLE
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        && RawShadowSampler != VK_NULL_HANDLE
        && RawPointShadowSampler != VK_NULL_HANDLE
#endif
        && ProbeSampler != VK_NULL_HANDLE;
}

VkImage LightBindings::GetAtlasImage() const
{
    return Images != nullptr ? Images->GetImage(Atlas) : VK_NULL_HANDLE;
}

VkImageView LightBindings::GetAtlasView() const
{
    return Images != nullptr ? Images->GetView(Atlas) : VK_NULL_HANDLE;
}

VkImage LightBindings::GetCubePoolImage() const
{
    return Images != nullptr ? Images->GetImage(CubePool) : VK_NULL_HANDLE;
}

VkImageView LightBindings::GetCubeFaceView(std::uint32_t slot,
                                           std::uint32_t face) const
{
    const std::uint32_t layer = slot * kPointShadowFaceCount + face;
    if (layer >= kMaxPointShadows * kPointShadowFaceCount)
        return VK_NULL_HANDLE;
    return CubeFaceViews[layer];
}

void LightBindings::TransitionAtlasForWrite(VkCommandBuffer commandBuffer)
{
    VulkanBarriers::ImageTransition transition{};
    transition.Image = GetAtlasImage();
    transition.OldLayout = AtlasLayout;
    transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    transition.SrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    transition.SrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    transition.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                         | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VulkanBarriers::TransitionImage(commandBuffer, transition);
    AtlasLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

void LightBindings::TransitionAtlasForRead(VkCommandBuffer commandBuffer)
{
    VulkanBarriers::ImageTransition transition{};
    transition.Image = GetAtlasImage();
    transition.OldLayout = AtlasLayout;
    transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    transition.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    transition.SrcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    transition.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VulkanBarriers::TransitionImage(commandBuffer, transition);
    AtlasLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}

void LightBindings::TransitionCubePoolForWrite(VkCommandBuffer commandBuffer)
{
    VulkanBarriers::ImageTransition transition{};
    transition.Image = GetCubePoolImage();
    transition.OldLayout = CubeLayout;
    transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    transition.SrcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    transition.SrcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    transition.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                         | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    transition.LayerCount = kMaxPointShadows * kPointShadowFaceCount;
    VulkanBarriers::TransitionImage(commandBuffer, transition);
    CubeLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

void LightBindings::TransitionCubePoolForRead(VkCommandBuffer commandBuffer)
{
    VulkanBarriers::ImageTransition transition{};
    transition.Image = GetCubePoolImage();
    transition.OldLayout = CubeLayout;
    transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    transition.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    transition.SrcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    transition.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    transition.LayerCount = kMaxPointShadows * kPointShadowFaceCount;
    VulkanBarriers::TransitionImage(commandBuffer, transition);
    CubeLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}
