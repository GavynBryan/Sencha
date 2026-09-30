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

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://content_picture.rml";
    desc.ModelName = "content_picture";
    desc.Properties = {
        UiModelProperty{ "image", UiValue(std::string{}) },  UiModelProperty{ "left", UiValue(std::string("0px")) },
        UiModelProperty{ "top", UiValue(std::string("0px")) }, UiModelProperty{ "width", UiValue(std::string("0px")) },
        UiModelProperty{ "height", UiValue(std::string("0px")) },
    };
    return desc;
}

std::string Px(float value)
{
    return std::format("{}px", value);
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

void ContentPicture::Place(const Rect2d& content)
{
    Content = content;
    if (!Screen.IsValid())
        return;
    (void)Ui.SetValue(Screen, kImage, UiValue(Name));
    (void)Ui.SetValue(Screen, kLeft, UiValue(Px(content.Position.X)));
    (void)Ui.SetValue(Screen, kTop, UiValue(Px(content.Position.Y)));
    (void)Ui.SetValue(Screen, kWidth, UiValue(Px(content.Width())));
    (void)Ui.SetValue(Screen, kHeight, UiValue(Px(content.Height())));
}

void ContentPicture::Open()
{
    if (Screen.IsValid() || !Surface.IsValid())
        return;
    Screen = Ui.OpenScreen(Surface, Describe());
    Place(Content);
}

void ContentPicture::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}
