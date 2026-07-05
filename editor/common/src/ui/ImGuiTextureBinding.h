#pragma once

#include <graphics/vulkan/VulkanImageService.h>
#include <render/TextureHandle.h>

#include <imgui.h>

#include <string>
#include <vector>

class AssetSystem;
class TextureCache;
class VulkanSamplerCache;

//=============================================================================
// ImGuiTextureBinding: displays a resident texture asset inside ImGui. Owns
// one asset reference (AssetSystem::LoadTexture/ReleaseTexture) and the ImGui
// Vulkan descriptor set for its image view, rebuilding the set whenever a hot
// reload swaps the entry's GPU image in place.
//
// Retired descriptor sets are freed a few TextureId() calls later, not
// immediately: an in-flight frame may still sample them (same rule as
// ViewportTargetCache). Release() must run while the asset system and the
// ImGui Vulkan backend are both alive; owners torn down after either must
// release explicitly (destruction order in editor apps puts render features
// after the asset system).
//=============================================================================
class ImGuiTextureBinding
{
public:
    ImGuiTextureBinding(AssetSystem& assets,
                        TextureCache& textures,
                        VulkanImageService& images,
                        VulkanSamplerCache& samplers);
    ~ImGuiTextureBinding();

    ImGuiTextureBinding(const ImGuiTextureBinding&) = delete;
    ImGuiTextureBinding& operator=(const ImGuiTextureBinding&) = delete;

    // Points the binding at a texture asset; an empty path just releases.
    // `nearestFilter` displays point-sampled (pixel art reads crisp).
    void SetTexture(const std::string& virtualPath, bool nearestFilter = false);

    // Drops the asset reference and frees all descriptor sets immediately.
    void Release();

    // The ImGui texture id for the current image, rebuilt if a reload swapped
    // it; 0 when nothing is bound or the texture failed to load.
    [[nodiscard]] ImTextureID TextureId();

    // Pixel size of the bound texture; {0,0} when unbound.
    [[nodiscard]] VkExtent2D Extent() const;

private:
    void RetireSet();

    AssetSystem& Assets;
    TextureCache& Textures;
    VulkanImageService& Images;
    VulkanSamplerCache& Samplers;

    std::string Path;
    bool Nearest = false;
    TextureHandle Handle;
    ImageHandle BoundImage;
    VkDescriptorSet Set = VK_NULL_HANDLE;

    // Sets waiting out possible in-flight sampling; Countdown ticks once per
    // TextureId() call (the panel's draw rate).
    struct RetiredSet
    {
        VkDescriptorSet Set = VK_NULL_HANDLE;
        uint32_t Countdown = 0;
    };
    std::vector<RetiredSet> Retired;
};
