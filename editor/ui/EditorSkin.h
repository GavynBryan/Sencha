#pragma once

#include <graphics/vulkan/VulkanImageService.h> // ImageHandle

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

class VulkanSamplerCache;

// One 9-slice skin element: its GPU texture, source size, and uniform border inset.
struct SkinElement
{
    ImTextureID Texture = 0;
    ImVec2 Size{};
    float Inset = 0.0f; // 9-slice border inset, in source pixels
    [[nodiscard]] bool Valid() const { return Texture != 0; }
};

// Loads the editor's 9-slice skin PNGs (editor/skin) into GPU textures and exposes
// them as ImTextureIDs for EditorUiSkin to draw. Built after the ImGui Vulkan
// backend is initialized and released before it shuts down (it allocates ImGui
// descriptor sets via ImGui_ImplVulkan_AddTexture). If any PNG is missing/bad,
// Loaded() is false and EditorUiSkin falls back to its gradient rendering — so the
// skin is a soft dependency and the editor still runs from a stripped checkout.
class EditorSkin
{
public:
    EditorSkin(VulkanImageService& images, VulkanSamplerCache& samplers, const std::string& skinDir);
    ~EditorSkin();

    EditorSkin(const EditorSkin&) = delete;
    EditorSkin& operator=(const EditorSkin&) = delete;

    [[nodiscard]] bool Loaded() const { return Frame.Valid() && Button.Valid() && Band.Valid(); }

    SkinElement Frame;  // panel frame (beveled border + dark interior)
    SkinElement Button; // button face (tinted per state)
    SkinElement Band;   // menu / tool / status metal band

private:
    SkinElement Load(const std::string& path, float inset);

    VulkanImageService& Images;
    VulkanSamplerCache& Samplers;
    std::vector<ImageHandle> OwnedImages;       // released on teardown
    std::vector<VkDescriptorSet> OwnedTextures; // ImGui_ImplVulkan_RemoveTexture targets
};
