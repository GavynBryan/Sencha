#include <graphics/vulkan/VulkanPipelineCache.h>

#include <graphics/vulkan/VulkanDeviceService.h>

namespace
{
    struct Fnv1a
    {
        uint64_t Value = 14695981039346656037ull;

        void Feed(const void* data, size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                Value ^= bytes[i];
                Value *= 1099511628211ull;
            }
        }

        template <typename T>
        void FeedPod(const T& value)
        {
            Feed(&value, sizeof(T));
        }
    };
}

VulkanPipelineCache::VulkanPipelineCache(LoggingProvider& logging,
                                         VulkanDeviceService& device,
                                         VulkanShaderCache& shaders)
    : Log(logging.GetLogger<VulkanPipelineCache>())
    , Device(device.GetDevice())
    , Shaders(&shaders)
{
    if (!device.IsValid() || !shaders.IsValid())
    {
        Log.Error("Cannot create VulkanPipelineCache: upstream services not valid");
        return;
    }

    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    const VkResult result = vkCreatePipelineCache(Device, &info, nullptr, &DriverCache);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreatePipelineCache failed ({})", static_cast<int>(result));
        return;
    }

    Valid = true;
}

VulkanPipelineCache::~VulkanPipelineCache()
{
    for (auto& entry : Entries)
        if (entry.Pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(Device, entry.Pipeline, nullptr);
    Entries.clear();

    if (DriverCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(Device, DriverCache, nullptr);
        DriverCache = VK_NULL_HANDLE;
    }
}

uint64_t VulkanPipelineCache::HashDesc(const GraphicsPipelineDesc& desc) const
{
    Fnv1a hash;
    hash.FeedPod(desc.VertexShader);
    hash.FeedPod(desc.FragmentShader);
    hash.FeedPod(desc.Layout);

    const uint32_t specializationCount =
        static_cast<uint32_t>(desc.FragmentSpecializationConstants.size());
    hash.FeedPod(specializationCount);
    for (const auto& constant : desc.FragmentSpecializationConstants)
        hash.FeedPod(constant);

    const uint32_t bindingCount = static_cast<uint32_t>(desc.VertexBindings.size());
    hash.FeedPod(bindingCount);
    for (const auto& binding : desc.VertexBindings)
        hash.FeedPod(binding);

    const uint32_t attributeCount = static_cast<uint32_t>(desc.VertexAttributes.size());
    hash.FeedPod(attributeCount);
    for (const auto& attribute : desc.VertexAttributes)
        hash.FeedPod(attribute);

    hash.FeedPod(desc.Topology);
    hash.FeedPod(desc.CullMode);
    hash.FeedPod(desc.FrontFace);
    hash.FeedPod(desc.PolygonMode);
    hash.FeedPod(desc.DepthTest);
    hash.FeedPod(desc.DepthWrite);
    hash.FeedPod(desc.DepthCompare);
    hash.FeedPod(desc.DepthBiasEnable);
    hash.FeedPod(desc.DepthBiasConstant);
    hash.FeedPod(desc.DepthBiasSlope);

    const uint32_t blendCount = static_cast<uint32_t>(desc.ColorBlend.size());
    hash.FeedPod(blendCount);
    for (const auto& blend : desc.ColorBlend)
        hash.FeedPod(blend);

    const uint32_t colorFormatCount = static_cast<uint32_t>(desc.ColorFormats.size());
    hash.FeedPod(colorFormatCount);
    for (const auto& format : desc.ColorFormats)
        hash.FeedPod(format);

    hash.FeedPod(desc.DepthFormat);
    hash.FeedPod(desc.StencilFormat);
    return hash.Value;
}

VkPipeline VulkanPipelineCache::GetGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    if (!Valid)
        return VK_NULL_HANDLE;

    const uint64_t hash = HashDesc(desc);
    for (auto& entry : Entries)
        if (entry.Hash == hash && entry.Desc == desc)
            return entry.Pipeline;

    VkPipeline pipeline = CreateGraphicsPipeline(desc);
    if (pipeline == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    Entries.push_back({ hash, desc, pipeline });
    return pipeline;
}

VkPipeline VulkanPipelineCache::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    const VkShaderModule vertexModule = Shaders->GetModule(desc.VertexShader);
    const VkShaderModule fragmentModule = Shaders->GetModule(desc.FragmentShader);
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE)
    {
        Log.Error("CreateGraphicsPipeline: invalid shader handle(s)");
        return VK_NULL_HANDLE;
    }
    if (desc.Layout == VK_NULL_HANDLE)
    {
        Log.Error("CreateGraphicsPipeline: GraphicsPipelineDesc.Layout is null");
        return VK_NULL_HANDLE;
    }

    std::vector<VkSpecializationMapEntry> specializationEntries;
    std::vector<uint32_t> specializationValues;
    specializationEntries.reserve(desc.FragmentSpecializationConstants.size());
    specializationValues.reserve(desc.FragmentSpecializationConstants.size());
    for (uint32_t index = 0;
         index < static_cast<uint32_t>(desc.FragmentSpecializationConstants.size());
         ++index)
    {
        const ShaderSpecializationConstant& constant =
            desc.FragmentSpecializationConstants[index];
        specializationEntries.push_back(VkSpecializationMapEntry{
            .constantID = constant.Id,
            .offset = index * static_cast<uint32_t>(sizeof(uint32_t)),
            .size = sizeof(uint32_t),
        });
        specializationValues.push_back(constant.Value);
    }

    VkSpecializationInfo fragmentSpecialization{};
    if (!specializationEntries.empty())
    {
        fragmentSpecialization.mapEntryCount =
            static_cast<uint32_t>(specializationEntries.size());
        fragmentSpecialization.pMapEntries = specializationEntries.data();
        fragmentSpecialization.dataSize = specializationValues.size() * sizeof(uint32_t);
        fragmentSpecialization.pData = specializationValues.data();
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";
    stages[1].pSpecializationInfo = specializationEntries.empty()
        ? nullptr
        : &fragmentSpecialization;

    std::vector<VkVertexInputBindingDescription> bindings;
    bindings.reserve(desc.VertexBindings.size());
    for (const auto& binding : desc.VertexBindings)
        bindings.push_back({ binding.Binding, binding.Stride, binding.InputRate });

    std::vector<VkVertexInputAttributeDescription> attributes;
    attributes.reserve(desc.VertexAttributes.size());
    for (const auto& attribute : desc.VertexAttributes)
        attributes.push_back({ attribute.Location, attribute.Binding,
                               attribute.Format, attribute.Offset });

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = desc.Topology;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = desc.PolygonMode;
    raster.cullMode = desc.CullMode;
    raster.frontFace = desc.FrontFace;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = desc.DepthBiasEnable ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.DepthBiasConstant;
    raster.depthBiasSlopeFactor = desc.DepthBiasSlope;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.DepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.DepthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = desc.DepthCompare;

    std::vector<VkPipelineColorBlendAttachmentState> blendStates;
    blendStates.reserve(desc.ColorBlend.size());
    for (const auto& blend : desc.ColorBlend)
    {
        VkPipelineColorBlendAttachmentState state{};
        state.blendEnable = blend.BlendEnable ? VK_TRUE : VK_FALSE;
        state.srcColorBlendFactor = blend.SrcColor;
        state.dstColorBlendFactor = blend.DstColor;
        state.colorBlendOp = blend.ColorOp;
        state.srcAlphaBlendFactor = blend.SrcAlpha;
        state.dstAlphaBlendFactor = blend.DstAlpha;
        state.alphaBlendOp = blend.AlphaOp;
        state.colorWriteMask = blend.WriteMask;
        blendStates.push_back(state);
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<uint32_t>(blendStates.size());
    colorBlend.pAttachments = blendStates.data();

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = static_cast<uint32_t>(desc.ColorFormats.size());
    rendering.pColorAttachmentFormats = desc.ColorFormats.data();
    rendering.depthAttachmentFormat = desc.DepthFormat;
    rendering.stencilAttachmentFormat = desc.StencilFormat;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = desc.Layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(
        Device, DriverCache, 1, &pipelineInfo, nullptr, &pipeline);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateGraphicsPipelines failed ({})", static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return pipeline;
}
