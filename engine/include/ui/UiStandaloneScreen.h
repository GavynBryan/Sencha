#pragma once

#include <graphics/RenderExtent.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <string_view>

class UiService;

class UiStandaloneScreen
{
public:
    UiStandaloneScreen() = default;
    UiStandaloneScreen(const UiStandaloneScreen&) = delete;
    UiStandaloneScreen& operator=(const UiStandaloneScreen&) = delete;

    bool OpenOverWindow(UiService* ui, std::string_view surfaceName, const UiScreenDesc& desc,
                        UiSurfaceInputPolicy input, UiStackOrder order = kUiStackOrderDefault);
    bool OpenInTexture(UiService* ui, std::string_view surfaceName, const UiScreenDesc& desc,
                       RenderExtent extent, std::string_view texture);
    void Close();

    [[nodiscard]] bool IsOpen() const { return Opened.IsValid(); }
    [[nodiscard]] UiService* Ui() const { return Service; }
    [[nodiscard]] UiScreenHandle Screen() const { return Opened; }
    [[nodiscard]] UiSurfaceId Surface() const { return OwnSurface; }

private:
    void OpenOn(UiService& ui, UiSurfaceId surface, const UiScreenDesc& desc);

    UiService* Service = nullptr;
    UiSurfaceId OwnSurface;
    UiScreenHandle Opened;
    bool Attempted = false;
};
