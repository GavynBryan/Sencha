#include "presenters/ContentPicture.h"

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

std::string Px(float value)
{
    return std::format("{}px", value);
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
{
}

ContentPicture::~ContentPicture()
{
    Close();
    Ui.RemoveHostImage(Name);
}

void ContentPicture::MoveTo(UiSurfaceId content)
{
    if (content == Surface)
        return;
    Close();
    Surface = content;
    if (Shown)
        Open();
}

void ContentPicture::SetShown(bool shown)
{
    Shown = shown;
    if (shown)
        Open();
    else
        Close();
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
    if (!Screen.IsValid())
        return;
    std::vector<UiRow> rows;
    rows.reserve(Frames.size());
    for (const PictureFrame& frame : Frames)
    {
        const Vec2d corner = frame.Rect.Position - Content.Position;
        UiRow row{ "", frame.Faint ? "faint" : "", std::string(RoleName(frame.Role)) };
        row.Parts = { Px(corner.X), Px(corner.Y), Px(frame.Rect.Width()), Px(frame.Rect.Height()) };
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kFrames, rows);
}

void ContentPicture::MarkDots(std::span<const PictureDot> dots)
{
    Dots.assign(dots.begin(), dots.end());
    PublishDots();
}

void ContentPicture::PublishDots()
{
    if (!Screen.IsValid())
        return;
    std::vector<UiRow> rows;
    rows.reserve(Dots.size());
    for (const PictureDot& dot : Dots)
    {
        const Vec2d corner = dot.Centre - Content.Position - Vec2d(dot.Radius, dot.Radius);
        UiRow row{ "", "", "" };
        row.Selected = dot.Selected;
        row.Parts = { Px(corner.X), Px(corner.Y), Px(dot.Radius * 2.0f) };
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kDots, rows);
}

void ContentPicture::Publish()
{
    if (!Screen.IsValid())
        return;
    const Vec2d corner = Picture.Position - Content.Position;
    (void)Ui.SetValue(Screen, kImage, UiValue(Name));
    (void)Ui.SetValue(Screen, kLeft, UiValue(Px(Content.Position.X)));
    (void)Ui.SetValue(Screen, kTop, UiValue(Px(Content.Position.Y)));
    (void)Ui.SetValue(Screen, kWidth, UiValue(Px(Content.Width())));
    (void)Ui.SetValue(Screen, kHeight, UiValue(Px(Content.Height())));
    (void)Ui.SetValue(Screen, kPictureLeft, UiValue(Px(corner.X)));
    (void)Ui.SetValue(Screen, kPictureTop, UiValue(Px(corner.Y)));
    (void)Ui.SetValue(Screen, kPictureWidth, UiValue(Px(Picture.Width())));
    (void)Ui.SetValue(Screen, kPictureHeight, UiValue(Px(Picture.Height())));
}

void ContentPicture::Open()
{
    if (Screen.IsValid() || !Surface.IsValid())
        return;
    Screen = Ui.OpenScreen(Surface, Describe());
    Publish();
    PublishFrames();
    PublishDots();
}

void ContentPicture::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}
