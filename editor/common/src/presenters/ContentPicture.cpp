#include "presenters/ContentPicture.h"

#include "ui/DocumentLengths.h"

#include <ui/UiService.h>

#include <format>

namespace
{
const UiModelPropertyId kImage = UiPropertyIdAt(0);
const UiModelPropertyId kLeft = UiPropertyIdAt(1);
const UiModelPropertyId kTop = UiPropertyIdAt(2);
const UiModelPropertyId kWidth = UiPropertyIdAt(3);
const UiModelPropertyId kHeight = UiPropertyIdAt(4);
const UiModelPropertyId kPictureLeft = UiPropertyIdAt(5);
const UiModelPropertyId kPictureTop = UiPropertyIdAt(6);
const UiModelPropertyId kPictureWidth = UiPropertyIdAt(7);
const UiModelPropertyId kPictureHeight = UiPropertyIdAt(8);
const UiModelRowsId kFrames = UiRowsIdAt(0);
const UiModelRowsId kDots = UiRowsIdAt(1);

UiModelProperty Pixels(const char* name)
{
    return UiModelProperty{ name, UiValue(std::string("0px")) };
}

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://content_picture.rml";
    desc.ModelName = "content_picture";
    desc.Properties = {
        UiModelProperty{ "image", UiValue(std::string{}) },
        Pixels("left"),
        Pixels("top"),
        Pixels("width"),
        Pixels("height"),
        Pixels("picture_left"),
        Pixels("picture_top"),
        Pixels("picture_width"),
        Pixels("picture_height"),
    };
    desc.RowLists = { "frames", "dots" };
    return desc;
}

std::string_view RoleName(PictureFrameRole role)
{
    switch (role)
    {
    case PictureFrameRole::Bounds:   return "bounds";
    case PictureFrameRole::Selected: return "selected";
    case PictureFrameRole::Hover:    return "hover";
    case PictureFrameRole::Caution:  return "caution";
    }
    return "bounds";
}
} // namespace

ContentPicture::ContentPicture(UiService& ui, std::string imageName)
    : Ui(ui)
    , Name(std::move(imageName))
    , HostImage(ui.SetHostImage(Name, RenderExtent{ 1, 1 }))
    , Screen(ui, Describe)
{
}

ContentPicture::~ContentPicture()
{
    Screen.Close();
    Ui.RemoveHostImage(Name);
}

void ContentPicture::MoveTo(UiSurfaceId content)
{
    if (content == Screen.Surface())
        return;
    Screen.Close();
    Screen.MoveTo(content);
    if (Shown)
        Open();
}

void ContentPicture::SetShown(bool shown)
{
    Shown = shown;
    if (shown)
        Open();
    else
        Screen.Close();
}

void ContentPicture::Place(const Rect2d& content, const Rect2d& picture)
{
    Content = content;
    Picture = picture;
    Publish();
}

void ContentPicture::Mark(std::span<const PictureFrame> frames)
{
    Frames.assign(frames.begin(), frames.end());
    PublishFrames();
}

void ContentPicture::PublishFrames()
{
    if (!Screen.IsOpen())
        return;
    std::vector<UiRow> rows;
    rows.reserve(Frames.size());
    for (const PictureFrame& frame : Frames)
    {
        const Vec2d corner = frame.Rect.Position - Content.Position;
        UiRow row{ "", frame.Faint ? "faint" : "", std::string(RoleName(frame.Role)) };
        row.Parts = { PixelLength(corner.X), PixelLength(corner.Y), PixelLength(frame.Rect.Width()), PixelLength(frame.Rect.Height()) };
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen.Handle(), kFrames, rows);
}

void ContentPicture::MarkDots(std::span<const PictureDot> dots)
{
    Dots.assign(dots.begin(), dots.end());
    PublishDots();
}

void ContentPicture::PublishDots()
{
    if (!Screen.IsOpen())
        return;
    std::vector<UiRow> rows;
    rows.reserve(Dots.size());
    for (const PictureDot& dot : Dots)
    {
        const Vec2d corner = dot.Centre - Content.Position - Vec2d(dot.Radius, dot.Radius);
        UiRow row{ "", "", "" };
        row.Selected = dot.Selected;
        row.Parts = { PixelLength(corner.X), PixelLength(corner.Y), PixelLength(dot.Radius * 2.0f) };
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen.Handle(), kDots, rows);
}

void ContentPicture::Publish()
{
    if (!Screen.IsOpen())
        return;
    const UiScreenHandle screen = Screen.Handle();
    const Vec2d corner = Picture.Position - Content.Position;
    (void)Ui.SetValue(screen, kImage, UiValue(Name));
    (void)Ui.SetValue(screen, kLeft, UiValue(PixelLength(Content.Position.X)));
    (void)Ui.SetValue(screen, kTop, UiValue(PixelLength(Content.Position.Y)));
    (void)Ui.SetValue(screen, kWidth, UiValue(PixelLength(Content.Width())));
    (void)Ui.SetValue(screen, kHeight, UiValue(PixelLength(Content.Height())));
    (void)Ui.SetValue(screen, kPictureLeft, UiValue(PixelLength(corner.X)));
    (void)Ui.SetValue(screen, kPictureTop, UiValue(PixelLength(corner.Y)));
    (void)Ui.SetValue(screen, kPictureWidth, UiValue(PixelLength(Picture.Width())));
    (void)Ui.SetValue(screen, kPictureHeight, UiValue(PixelLength(Picture.Height())));
}

void ContentPicture::Open()
{
    if (Screen.IsOpen() || !Screen.Open())
        return;
    Publish();
    PublishFrames();
    PublishDots();
}
