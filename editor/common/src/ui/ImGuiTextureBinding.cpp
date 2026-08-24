#include "ui/ImGuiTextureBinding.h"

#include <assets/runtime/AssetSystem.h>
#include <assets/texture/TextureCache.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <imgui_impl_vulkan.h>

namespace
{
    // Longer than any realistic frames-in-flight count.
    constexpr uint32_t kRetireTicks = 4;
}

ImGuiTextureBinding::ImGuiTextureBinding(AssetSystem& assets,
                                         TextureCache& textures,
                                         VulkanImageService& images,
                                         VulkanSamplerCache& samplers)
    : Assets(assets)
    , Textures(textures)
    , Images(images)
    , Samplers(samplers)
{
}

ImGuiTextureBinding::~ImGuiTextureBinding()
{
    Release();
}

void ImGuiTextureBinding::SetTexture(const std::string& virtualPath, bool nearestFilter)
{
    if (virtualPath == Path && nearestFilter == Nearest)
        return;

    if (Handle.IsValid())
        Assets.ReleaseTexture(Handle);
    Handle = {};
    BoundImage = {};
    RetireSet();

    Path = virtualPath;
    Nearest = nearestFilter;
    if (!Path.empty())
        Handle = Assets.LoadTexture(Path);
}

void ImGuiTextureBinding::Release()
{
    if (Handle.IsValid())
        Assets.ReleaseTexture(Handle);
    Handle = {};
    BoundImage = {};
    Path.clear();

    // Immediate frees: Release runs at teardown (or explicit reset), where
    // the owner has already stopped drawing with these sets.
    if (Set != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_RemoveTexture(Set);
        Set = VK_NULL_HANDLE;
    }
    for (const RetiredSet& retired : Retired)
        ImGui_ImplVulkan_RemoveTexture(retired.Set);
    Retired.clear();
}

ImTextureID ImGuiTextureBinding::TextureId()
{
    for (auto it = Retired.begin(); it != Retired.end();)
    {
        if (it->Countdown-- == 0)
        {
            ImGui_ImplVulkan_RemoveTexture(it->Set);
            it = Retired.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (!Handle.IsValid())
        return 0;

    const ImageHandle current = Textures.GetGpuImage(Handle);
    if (!current.IsValid())
        return 0;

    if (current != BoundImage || Set == VK_NULL_HANDLE)
    {
        RetireSet();
        const VkSampler sampler = Nearest ? Samplers.GetNearestClamp()
                                          : Samplers.GetLinearClamp();
        Set = ImGui_ImplVulkan_AddTexture(sampler, Images.GetView(current),
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        BoundImage = current;
    }

    // VkDescriptorSet -> ImTextureID (ImU64), the Vulkan backend's convention.
    return Set != VK_NULL_HANDLE ? (ImTextureID)Set : (ImTextureID)0;
}

VkExtent2D ImGuiTextureBinding::Extent() const
{
    if (!Handle.IsValid())
        return {};
    const RenderExtent extent = Textures.GetExtent(Handle);
    return { extent.Width, extent.Height };
}

void ImGuiTextureBinding::RetireSet()
{
    if (Set == VK_NULL_HANDLE)
        return;
    Retired.push_back(RetiredSet{ Set, kRetireTicks });
    Set = VK_NULL_HANDLE;
}
