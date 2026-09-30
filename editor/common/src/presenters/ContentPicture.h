#pragma once

#include <math/geometry/2d/Rect2d.h>
#include <render/ui/UiDrawFrame.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <string>

class UiService;

//=============================================================================
// ContentPicture
//
// One host image filling a workspace's content: a preview the workspace
// renders offscreen, shown on the window's content surface where the window
// says the content goes. It names the image and places it; the workspace's
// renderer fills it at the size it asks for.
//=============================================================================
class ContentPicture
{
public:
    // `imageName` is the host image's name, unique among the application's.
    ContentPicture(UiService& ui, std::string imageName);
    ~ContentPicture();

    ContentPicture(const ContentPicture&) = delete;
    ContentPicture& operator=(const ContentPicture&) = delete;

    void MoveTo(UiSurfaceId content);
    void SetShown(bool shown);
    // Per frame: where the content is, in window pixels.
    void Place(const Rect2d& content);

    [[nodiscard]] UiHostImageId Image() const { return HostImage; }
    [[nodiscard]] const Rect2d& Placed() const { return Content; }

private:
    void Open();
    void Close();

    UiService& Ui;
    std::string Name;
    UiHostImageId HostImage;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
    Rect2d Content{};
    bool Shown = false;
};
