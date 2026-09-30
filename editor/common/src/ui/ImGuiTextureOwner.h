#pragma once

#include <vulkan/vulkan.h>

struct ImGuiContext;

// Every ImGui texture set the editor adds or removes goes through one context's
// Vulkan backend -- the primary window's -- whichever window's context happens
// to be current, because a set is freed through the pool that allocated it.
void SetImGuiTextureOwner(ImGuiContext* owner);
[[nodiscard]] VkDescriptorSet AddImGuiTexture(VkSampler sampler, VkImageView view, VkImageLayout layout);
void RemoveImGuiTexture(VkDescriptorSet set);

// Makes `context` current for its lifetime and restores the one before it.
class ScopedImGuiContext
{
public:
    explicit ScopedImGuiContext(ImGuiContext* context);
    ~ScopedImGuiContext();
    ScopedImGuiContext(const ScopedImGuiContext&) = delete;
    ScopedImGuiContext& operator=(const ScopedImGuiContext&) = delete;

private:
    ImGuiContext* Previous = nullptr;
};
