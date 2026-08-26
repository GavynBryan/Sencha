#include "ImGuiTargetPresenter.h"

#include <imgui_impl_vulkan.h>

#include <algorithm>

void ImGuiTargetPresenter::Setup(VkSampler sampler)
{
    Sampler = sampler;
}

void ImGuiTargetPresenter::Teardown()
{
    for (Binding& binding : Bindings)
        Retire(binding.Set);
    Bindings.clear();
    Flush(/*force*/ true);
}

void ImGuiTargetPresenter::BeginFrame(GpuFrameRetirement retirement)
{
    Retirement = retirement;
    Flush(/*force*/ false);
}

void ImGuiTargetPresenter::Retire(VkDescriptorSet set)
{
    if (set != VK_NULL_HANDLE)
        RetiredSets.push_back({ set, Retirement.Stamp() });
}

void ImGuiTargetPresenter::Flush(bool force)
{
    for (auto it = RetiredSets.begin(); it != RetiredSets.end();)
    {
        if (force || Retirement.IsRetired(it->Stamp))
        {
            ImGui_ImplVulkan_RemoveTexture(it->Set);
            it = RetiredSets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

ImTextureID ImGuiTargetPresenter::Present(RenderTargetStore& store, RenderTargetId id)
{
    // Peek, never Acquire: building here would hand ImGui an image in
    // UNDEFINED layout that nothing has rendered into, and sampling that is a
    // validation error rather than a blank frame.
    const std::optional<RenderTargetView> view = store.Peek(id);
    if (!view.has_value() || view->ColorView == VK_NULL_HANDLE)
        return (ImTextureID)0;
    // Built but not yet written -- a slot rebuilt by a resize whose render did
    // not follow. The layout the store remembers is the record of that.
    if (view->ColorLayout == nullptr || *view->ColorLayout == VK_IMAGE_LAYOUT_UNDEFINED)
        return (ImTextureID)0;

    const auto it = std::find_if(Bindings.begin(), Bindings.end(),
                                 [id](const Binding& b) { return b.Id == id; });
    Binding& binding = it != Bindings.end()
        ? *it
        : Bindings.emplace_back(Binding{ .Id = id });

    if (binding.View != view->ColorView)
    {
        Retire(binding.Set);
        binding.View = view->ColorView;
        binding.Set = ImGui_ImplVulkan_AddTexture(Sampler, view->ColorView,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // VkDescriptorSet -> ImTextureID (ImU64), the ImGui Vulkan backend's
    // convention.
    return binding.Set != VK_NULL_HANDLE ? (ImTextureID)binding.Set : (ImTextureID)0;
}

void ImGuiTargetPresenter::Release(RenderTargetId id)
{
    const auto it = std::find_if(Bindings.begin(), Bindings.end(),
                                 [id](const Binding& b) { return b.Id == id; });
    if (it == Bindings.end())
        return;
    Retire(it->Set);
    Bindings.erase(it);
}
