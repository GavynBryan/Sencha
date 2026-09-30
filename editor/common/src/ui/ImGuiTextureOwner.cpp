#include "ui/ImGuiTextureOwner.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

namespace
{
    ImGuiContext* TextureOwner = nullptr;
}

void SetImGuiTextureOwner(ImGuiContext* owner)
{
    TextureOwner = owner;
}

VkDescriptorSet AddImGuiTexture(VkSampler sampler, VkImageView view, VkImageLayout layout)
{
    const ScopedImGuiContext owner(TextureOwner != nullptr ? TextureOwner : ImGui::GetCurrentContext());
    return ImGui_ImplVulkan_AddTexture(sampler, view, layout);
}

void RemoveImGuiTexture(VkDescriptorSet set)
{
    const ScopedImGuiContext owner(TextureOwner != nullptr ? TextureOwner : ImGui::GetCurrentContext());
    ImGui_ImplVulkan_RemoveTexture(set);
}

ScopedImGuiContext::ScopedImGuiContext(ImGuiContext* context)
    : Previous(ImGui::GetCurrentContext())
{
    ImGui::SetCurrentContext(context);
}

ScopedImGuiContext::~ScopedImGuiContext()
{
    // A context that was the first made has nothing before it; it stays current.
    if (Previous != nullptr)
        ImGui::SetCurrentContext(Previous);
}
