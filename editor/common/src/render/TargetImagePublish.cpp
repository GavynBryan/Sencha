#include "render/TargetImagePublish.h"

void PublishTargetImage(UiTextureResidency& textures, UiHostImageId image, const VkImageLayout* colorLayout,
                        std::uint32_t bindless)
{
    const bool readable = colorLayout != nullptr && *colorLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                       && bindless != UINT32_MAX;
    if (readable)
        textures.PublishHostImage(image, BindlessImageIndex{ bindless });
    else
        textures.WithdrawHostImage(image);
}
