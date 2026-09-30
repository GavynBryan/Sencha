#include <graphics/vulkan/PresentationWindows.h>

#include <graphics/vulkan/GraphicsServices.h>
#include <graphics/vulkan/PresentationTarget.h>
#include <platform/SdlWindow.h>
#include <platform/SdlWindowService.h>
#include <platform/WindowCreateInfo.h>

PresentationId OpenPresentationWindow(GraphicsServices& graphics, SdlWindowService& windows,
                                      const WindowCreateInfo& window, const PresentationDesc& desc)
{
    WindowCreateInfo vulkanWindow = window;
    vulkanWindow.GraphicsApi = WindowGraphicsApi::Vulkan;
    SdlWindow* created = windows.CreateWindow(vulkanWindow);
    if (created == nullptr)
        return {};
    const PresentationId presentation = graphics.MainRenderer.CreatePresentation(*created, desc);
    if (presentation.IsNull())
        windows.DestroyWindow(created->GetId());
    return presentation;
}

bool ClosePresentationWindow(GraphicsServices& graphics, SdlWindowService& windows, PresentationId presentation)
{
    PresentationTarget* target = graphics.Frames.FindPresentation(presentation);
    if (target == nullptr)
        return false;
    SdlWindow& window = target->Window();
    const std::uint32_t windowId = window.GetId();
    // The surface must die before the native window it was made from.
    if (!graphics.MainRenderer.DestroyPresentation(presentation,
                                                   [&windows, windowId] { windows.DestroyWindow(windowId); }))
        return false;
    window.Hide();
    return true;
}
