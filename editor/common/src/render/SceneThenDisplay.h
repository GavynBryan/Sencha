#pragma once

#include <graphics/vulkan/OutputTransformPass.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/Renderer.h>
#include <render/OutputTransformParams.h>

#include <functional>

// The images one editor view draws into. The scene target holds scene-referred
// light for the output pass; the display target is what the UI samples.
struct SceneDisplayTargets
{
    VkExtent2D Extent{};
    VkImage SceneImage = VK_NULL_HANDLE;
    VkImageView SceneView = VK_NULL_HANDLE;
    RenderTargetAccess* SceneAccess = nullptr;
    std::uint32_t SceneBindless = UINT32_MAX;
    VkImage DisplayImage = VK_NULL_HANDLE;
    VkImageView DisplayView = VK_NULL_HANDLE;
    VkImageLayout* DisplayLayout = nullptr;
    VkImage DepthImage = VK_NULL_HANDLE;
    VkImageView DepthView = VK_NULL_HANDLE;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
};

// Orders a later scope's use of a view's depth after the last scope's writes to it,
// with the layout unchanged. A load or store op of DONT_CARE still counts as a write.
void OrderViewDepthAccess(VkCommandBuffer cmd, VkImage depth, VkFormat format);

// One view's frame: `drawScene` into the scene target, `drawBetween` outside any scope
// with the scene's depth readable, the output pass, then `drawDisplay` over its result
// with that depth, in display-referred linear light. The one path an editor view renders
// through, so none can skip the output pass. The display target ends readable.
void RecordSceneThenDisplay(const FrameContext& frame, const SceneDisplayTargets& targets, VkClearColorValue clear,
                            OutputTransformPass& output, const OutputTransformParams& params,
                            const std::function<void(const FrameContext&)>& drawScene,
                            const std::function<void(const FrameContext&)>& drawDisplay,
                            const std::function<void(const FrameContext&)>& drawBetween = {},
                            ScenePostState* post = nullptr);
