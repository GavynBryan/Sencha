#include "EditorSkin.h"

#include <graphics/vulkan/VulkanSamplerCache.h>
#include <render/ImageLoader.h>

#include <imgui_impl_vulkan.h>

EditorSkin::EditorSkin(VulkanImageService& images, VulkanSamplerCache& samplers, const std::string& skinDir)
    : Images(images)
    , Samplers(samplers)
{
    // Insets match the borders baked by scripts/gen_editor_skin.py.
    Frame  = Load(skinDir + "/frame.png", 12.0f);
    Button = Load(skinDir + "/button.png", 8.0f);
    Band   = Load(skinDir + "/band.png", 6.0f);
}

EditorSkin::~EditorSkin()
{
    for (VkDescriptorSet set : OwnedTextures)
        ImGui_ImplVulkan_RemoveTexture(set);
    for (ImageHandle handle : OwnedImages)
        Images.Destroy(handle);
}

SkinElement EditorSkin::Load(const std::string& path, float inset)
{
    // Linear (UNORM) so the authored bytes blend exactly like ImGui's vertex colors
    // and the palette — no GPU sRGB linearization fighting the look.
    const std::optional<Image> image = LoadImageFromFile(path, /*srgb=*/false);
    if (!image)
        return {};

    ImageCreateInfo info;
    info.Format = VK_FORMAT_R8G8B8A8_UNORM;
    info.Extent = VkExtent2D{ image->Width, image->Height };
    info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    info.DebugName = "editor_skin";

    const ImageHandle handle = Images.Create(info);
    if (handle.IsNull())
        return {};
    if (!Images.Upload(handle, image->Pixels.data(), image->Pixels.size()))
    {
        Images.Destroy(handle);
        return {};
    }
    OwnedImages.push_back(handle);

    const VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
        Samplers.GetLinearClamp(), Images.GetView(handle), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    OwnedTextures.push_back(set);

    SkinElement element;
    // VkDescriptorSet -> ImTextureID (ImU64), the Vulkan backend's convention.
    element.Texture = (ImTextureID)set;
    element.Size = ImVec2(static_cast<float>(image->Width), static_cast<float>(image->Height));
    element.Inset = inset;
    return element;
}
