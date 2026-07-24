#pragma once

#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

class VulkanDeviceService;

//=============================================================================
// VulkanPipelineCache
//
// Caches VkPipeline objects by content-hashing a GraphicsPipelineDesc. The
// cache owns every pipeline it hands out; callers receive raw VkPipelines and
// must not destroy them. Lifetime is service-scoped.
//=============================================================================

struct VertexInputBindingDesc
{
    uint32_t Binding = 0;
    uint32_t Stride = 0;
    VkVertexInputRate InputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    bool operator==(const VertexInputBindingDesc&) const = default;
};

struct VertexInputAttributeDesc
{
    uint32_t Location = 0;
    uint32_t Binding = 0;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Offset = 0;

    bool operator==(const VertexInputAttributeDesc&) const = default;
};

struct ShaderSpecializationConstant
{
    uint32_t Id = 0;
    uint32_t Value = 0;

    bool operator==(const ShaderSpecializationConstant&) const = default;
};

struct ColorBlendAttachmentDesc
{
    bool BlendEnable = false;
    VkBlendFactor SrcColor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor DstColor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp ColorOp = VK_BLEND_OP_ADD;
    VkBlendFactor SrcAlpha = VK_BLEND_FACTOR_ONE;
    VkBlendFactor DstAlpha = VK_BLEND_FACTOR_ZERO;
    VkBlendOp AlphaOp = VK_BLEND_OP_ADD;
    VkColorComponentFlags WriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    bool operator==(const ColorBlendAttachmentDesc&) const = default;
};

struct GraphicsPipelineDesc
{
    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout Layout = VK_NULL_HANDLE;

    std::vector<ShaderSpecializationConstant> FragmentSpecializationConstants;
    std::vector<VertexInputBindingDesc> VertexBindings;
    std::vector<VertexInputAttributeDesc> VertexAttributes;

    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags CullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;

    bool DepthTest = false;
    bool DepthWrite = false;
    VkCompareOp DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;

    bool DepthBiasEnable = false;
    float DepthBiasConstant = 0.0f;
    float DepthBiasSlope = 0.0f;

    std::vector<ColorBlendAttachmentDesc> ColorBlend;
    std::vector<VkFormat> ColorFormats;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkFormat StencilFormat = VK_FORMAT_UNDEFINED;

    bool operator==(const GraphicsPipelineDesc&) const = default;
};

class VulkanPipelineCache
{
public:
    VulkanPipelineCache(LoggingProvider& logging,
                        VulkanDeviceService& device,
                        VulkanShaderCache& shaders);
    ~VulkanPipelineCache();

    VulkanPipelineCache(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache(VulkanPipelineCache&&) = delete;
    VulkanPipelineCache& operator=(VulkanPipelineCache&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }
    [[nodiscard]] VkPipeline GetGraphicsPipeline(const GraphicsPipelineDesc& desc);

private:
    struct Entry
    {
        uint64_t Hash = 0;
        GraphicsPipelineDesc Desc;
        VkPipeline Pipeline = VK_NULL_HANDLE;
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    VulkanShaderCache* Shaders = nullptr;
    VkPipelineCache DriverCache = VK_NULL_HANDLE;
    bool Valid = false;

    std::vector<Entry> Entries;

    [[nodiscard]] uint64_t HashDesc(const GraphicsPipelineDesc& desc) const;
    [[nodiscard]] VkPipeline CreateGraphicsPipeline(const GraphicsPipelineDesc& desc);
};
