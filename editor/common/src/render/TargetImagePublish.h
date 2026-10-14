#pragma once

#include <graphics/vulkan/UiTextureResidency.h>

#include <vulkan/vulkan.h>

#include <cstdint>

// Shows a render target through a host image once this frame left it readable,
// and withdraws the image while it is not, so a document never samples a
// target mid-write or in an unknown layout.
void PublishTargetImage(UiTextureResidency& textures, UiHostImageId image, const VkImageLayout* colorLayout,
                        std::uint32_t bindless);
