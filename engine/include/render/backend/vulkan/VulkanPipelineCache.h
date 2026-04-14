#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <render/backend/vulkan/VulkanShaderCache.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <vector>

class VulkanDeviceService;

//=============================================================================
// VulkanPipelineCache
//
// Caches VkPipeline objects by content-hashing a GraphicsPipelineDesc. The
// cache owns every pipeline it hands out; callers receive raw VkPipelines
// and must not destroy them. Lifetime is service-scoped: pipelines live
// until the cache is destroyed.
//
// Dynamic rendering only -- the desc captures color + depth + stencil
// formats so the pipeline matches whatever render target the frame uses,
// with no VkRenderPass in sight.
//
// Viewport and scissor are always dynamic state. Swapchain resize does not
// invalidate the cache.
//
// Two layers of caching are in play:
//
//   1. The content-hashed VkPipeline table in this service. Second lookup
//      of the same desc in the same process is a hash-table hit.
//
//   2. The driver-level VkPipelineCache that backs every vkCreate*Pipelines
//      call. SaveToDisk / LoadFromDisk serialize this blob so a second
//      launch of the game skips the driver-backend compile entirely. This
//      is the single most important piece of shader-startup-stutter
//      mitigation Sencha ships.
//
// Pipeline layouts are owned externally (by descriptor cache / feature
// setup code) and passed in on the desc. The pipeline cache doesn't care
// where they came from.
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

    std::vector<VertexInputBindingDesc> VertexBindings;
    std::vector<VertexInputAttributeDesc> VertexAttributes;

    VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags CullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode PolygonMode = VK_POLYGON_MODE_FILL;

    bool DepthTest = false;
    bool DepthWrite = false;
    VkCompareOp DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::vector<ColorBlendAttachmentDesc> ColorBlend;
    std::vector<VkFormat> ColorFormats;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    VkFormat StencilFormat = VK_FORMAT_UNDEFINED;

    bool operator==(const GraphicsPipelineDesc&) const = default;
};

class VulkanPipelineCache : public IService
{
public:
    VulkanPipelineCache(LoggingProvider& logging,
                        VulkanDeviceService& device,
                        VulkanShaderCache& shaders);
    ~VulkanPipelineCache() override;

    VulkanPipelineCache(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache(VulkanPipelineCache&&) = delete;
    VulkanPipelineCache& operator=(VulkanPipelineCache&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // Returns an existing pipeline for this desc or creates a new one.
    // Returned VkPipeline is owned by the cache.
    [[nodiscard]] VkPipeline GetGraphicsPipeline(const GraphicsPipelineDesc& desc);

    // -- Driver cache persistence -------------------------------------------
    //
    // Serialize / deserialize the underlying VkPipelineCache blob. Call
    // LoadFromDisk once at startup before building any pipelines, and
    // SaveToDisk at shutdown. Corrupt or version-mismatched blobs are
    // handled by the driver and safely ignored.
    [[nodiscard]] bool LoadFromDisk(const std::filesystem::path& path);
    [[nodiscard]] bool SaveToDisk(const std::filesystem::path& path) const;

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
