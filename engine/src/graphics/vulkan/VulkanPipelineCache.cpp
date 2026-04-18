#include <graphics/vulkan/VulkanPipelineCache.h>

#include <graphics/vulkan/VulkanDeviceService.h>

#include <cstring>
#include <fstream>
#include <system_error>

namespace
{
    struct Fnv1a
    {
        uint64_t Value = 14695981039346656037ull;

        void Feed(const void* data, size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                Value ^= p[i];
                Value *= 1099511628211ull;
            }
        }

        template <typename T>
        void FeedPod(const T& v)
        {
            Feed(&v, sizeof(T));
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
    {
        if (entry.Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device, entry.Pipeline, nullptr);
        }
    }
    Entries.clear();

    if (DriverCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(Device, DriverCache, nullptr);
        DriverCache = VK_NULL_HANDLE;
    }
}

uint64_t VulkanPipelineCache::HashDesc(const GraphicsPipelineDesc& desc) const
{
    Fnv1a h;
    h.FeedPod(desc.VertexShader.Id);
    h.FeedPod(desc.FragmentShader.Id);
    h.FeedPod(desc.Layout);

    const uint32_t bindingCount = static_cast<uint32_t>(desc.VertexBindings.size());
    h.FeedPod(bindingCount);
    for (const auto& b : desc.VertexBindings) h.FeedPod(b);

    const uint32_t attrCount = static_cast<uint32_t>(desc.VertexAttributes.size());
    h.FeedPod(attrCount);
    for (const auto& a : desc.VertexAttributes) h.FeedPod(a);

    h.FeedPod(desc.Topology);
    h.FeedPod(desc.CullMode);
    h.FeedPod(desc.FrontFace);
    h.FeedPod(desc.PolygonMode);

    h.FeedPod(desc.DepthTest);
    h.FeedPod(desc.DepthWrite);
    h.FeedPod(desc.DepthCompare);

    const uint32_t blendCount = static_cast<uint32_t>(desc.ColorBlend.size());
    h.FeedPod(blendCount);
    for (const auto& cb : desc.ColorBlend) h.FeedPod(cb);

    const uint32_t colorFormatCount = static_cast<uint32_t>(desc.ColorFormats.size());
    h.FeedPod(colorFormatCount);
    for (const auto& f : desc.ColorFormats) h.FeedPod(f);

    h.FeedPod(desc.DepthFormat);
    h.FeedPod(desc.StencilFormat);

    return h.Value;
}

VkPipeline VulkanPipelineCache::GetGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    if (!Valid) return VK_NULL_HANDLE;

    const uint64_t hash = HashDesc(desc);
    for (auto& entry : Entries)
    {
        if (entry.Hash == hash && entry.Desc == desc)
        {
            return entry.Pipeline;
        }
    }

    VkPipeline pipeline = CreateGraphicsPipeline(desc);
    if (pipeline == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    Entries.push_back({ hash, desc, pipeline });
    return pipeline;
}

VkPipeline VulkanPipelineCache::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    VkShaderModule vsModule = Shaders->GetModule(desc.VertexShader);
    VkShaderModule fsModule = Shaders->GetModule(desc.FragmentShader);
    if (vsModule == VK_NULL_HANDLE || fsModule == VK_NULL_HANDLE)
    {
        Log.Error("CreateGraphicsPipeline: invalid shader handle(s)");
        return VK_NULL_HANDLE;
    }
    if (desc.Layout == VK_NULL_HANDLE)
    {
        Log.Error("CreateGraphicsPipeline: GraphicsPipelineDesc.Layout is null");
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsModule;
    stages[1].pName = "main";

    std::vector<VkVertexInputBindingDescription> vkBindings;
    vkBindings.reserve(desc.VertexBindings.size());
    for (const auto& b : desc.VertexBindings)
    {
        vkBindings.push_back({ b.Binding, b.Stride, b.InputRate });
    }

    std::vector<VkVertexInputAttributeDescription> vkAttrs;
    vkAttrs.reserve(desc.VertexAttributes.size());
    for (const auto& a : desc.VertexAttributes)
    {
        vkAttrs.push_back({ a.Location, a.Binding, a.Format, a.Offset });
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vkBindings.size());
    vertexInput.pVertexBindingDescriptions = vkBindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vkAttrs.size());
    vertexInput.pVertexAttributeDescriptions = vkAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = desc.Topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.DepthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.DepthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = desc.DepthCompare;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    std::vector<VkPipelineColorBlendAttachmentState> blendStates;
    blendStates.reserve(desc.ColorBlend.size());
    for (const auto& cb : desc.ColorBlend)
    {
        VkPipelineColorBlendAttachmentState s{};
        s.blendEnable = cb.BlendEnable ? VK_TRUE : VK_FALSE;
        s.srcColorBlendFactor = cb.SrcColor;
        s.dstColorBlendFactor = cb.DstColor;
        s.colorBlendOp = cb.ColorOp;
        s.srcAlphaBlendFactor = cb.SrcAlpha;
        s.dstAlphaBlendFactor = cb.DstAlpha;
        s.alphaBlendOp = cb.AlphaOp;
        s.colorWriteMask = cb.WriteMask;
        blendStates.push_back(s);
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
    pipelineInfo.renderPass = VK_NULL_HANDLE; // dynamic rendering
    pipelineInfo.subpass = 0;

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

bool VulkanPipelineCache::LoadFromDisk(const std::filesystem::path& path)
{
    if (!Valid) return false;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        // Missing cache file is the normal first-run case.
        return false;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        Log.Warn("PipelineCache: failed to open {} for reading", path.generic_string());
        return false;
    }

    const std::streamsize bytes = file.tellg();
    if (bytes <= 0)
    {
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> blob(static_cast<size_t>(bytes));
    file.read(reinterpret_cast<char*>(blob.data()), bytes);
    if (!file.good() && !file.eof())
    {
        Log.Warn("PipelineCache: short read from {}", path.generic_string());
        return false;
    }

    // Recreate the driver cache seeded with the blob. If the driver rejects
    // the data (version mismatch, different GPU, etc.), it either ignores
    // the seed or fails -- in either case we fall back to an empty cache.
    VkPipelineCache newCache = VK_NULL_HANDLE;
    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = blob.size();
    info.pInitialData = blob.data();
    const VkResult result = vkCreatePipelineCache(Device, &info, nullptr, &newCache);
    if (result != VK_SUCCESS)
    {
        Log.Warn("PipelineCache: driver rejected blob from {} ({})",
                 path.generic_string(), static_cast<int>(result));
        return false;
    }

    if (DriverCache != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(Device, DriverCache, nullptr);
    }
    DriverCache = newCache;
    return true;
}

bool VulkanPipelineCache::SaveToDisk(const std::filesystem::path& path) const
{
    if (!Valid || DriverCache == VK_NULL_HANDLE) return false;

    size_t bytes = 0;
    VkResult result = vkGetPipelineCacheData(Device, DriverCache, &bytes, nullptr);
    if (result != VK_SUCCESS || bytes == 0)
    {
        return false;
    }

    std::vector<uint8_t> blob(bytes);
    result = vkGetPipelineCacheData(Device, DriverCache, &bytes, blob.data());
    if (result != VK_SUCCESS)
    {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        Log.Warn("PipelineCache: failed to open {} for writing", path.generic_string());
        return false;
    }
    file.write(reinterpret_cast<const char*>(blob.data()),
               static_cast<std::streamsize>(blob.size()));
    return file.good();
}
