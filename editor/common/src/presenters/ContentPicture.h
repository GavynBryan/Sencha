#pragma once

#include <math/geometry/2d/Rect2d.h>
#include <render/ui/UiDrawFrame.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

class UiService;

// A rectangle marked over the picture, in window pixels, and what it marks.
enum class PictureFrameRole : std::uint8_t
{
    Bounds,
    Selected,
    Hover,
    Caution,
};

struct PictureFrame
{
    Rect2d Rect;
    PictureFrameRole Role = PictureFrameRole::Bounds;
    // Drawn fainter: a box inside another of the same role.
    bool Faint = false;
};

// A point marked on the picture, in window pixels: a joint, a handle.
struct PictureDot
{
    Vec2d Centre = {};
    float Radius = 3.0f;
    bool Selected = false;
};

//=============================================================================
// ContentPicture
//
// One host image in a workspace's content: a preview the workspace renders
// offscreen, shown on the window's content surface where the window says the
// content goes, and cut off at its edges. It names the image and places it,
// filling the content or wherever the workspace puts it, with frames marked
// and dots over it; the workspace's renderer fills it at the size it asks for.
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
    // Per frame: where the content is, in window pixels, and the picture in it.
    void Place(const Rect2d& content) { Place(content, content); }
    void Place(const Rect2d& content, const Rect2d& picture);
    void Mark(std::span<const PictureFrame> frames);
    void MarkDots(std::span<const PictureDot> dots);

    [[nodiscard]] UiHostImageId Image() const { return HostImage; }
    [[nodiscard]] const Rect2d& Placed() const { return Picture; }

private:
    void Open();
    void Close();
    void Publish();
    void PublishFrames();
    void PublishDots();

    UiService& Ui;
    std::string Name;
    UiHostImageId HostImage;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
    Rect2d Content{};
    Rect2d Picture{};
    std::vector<PictureFrame> Frames;
    std::vector<PictureDot> Dots;
    bool Shown = false;
};
