#pragma once

#include <render/backend/vulkan/VulkanPipelineCache.h>

#include <string>
#include <string_view>

//=============================================================================
// ParseShaderMetadataToDesc
//
// Parses a .shader JSON file and fills a GraphicsPipelineDesc with the
// pipeline state it describes.
//
// The returned desc contains everything derived from the metadata file:
//   - VertexBindings  (binding number, stride, input rate)
//   - Topology, CullMode, FrontFace, PolygonMode
//   - DepthTest, DepthWrite, DepthCompare
//   - ColorBlend attachments
//
// Fields intentionally NOT set (filled by the owning feature before calling
// VulkanPipelineCache::GetGraphicsPipeline):
//   - VertexShader, FragmentShader  -- handles from VulkanShaderCache
//   - Layout                        -- VkPipelineLayout from VulkanDescriptorCache
//   - VertexAttributes              -- locations/formats/offsets tied to the C++ vertex struct
//   - ColorFormats, DepthFormat, StencilFormat -- known only at draw time
//
// Returns true on success.  On failure returns false and writes a human-
// readable diagnostic to `error`.  The `out` desc is left in an unspecified
// state on failure.
//
// Thread safety: stateless pure function, safe to call from any thread.
//=============================================================================
[[nodiscard]] bool ParseShaderMetadataToDesc(std::string_view json,
                                              GraphicsPipelineDesc& out,
                                              std::string& error);
