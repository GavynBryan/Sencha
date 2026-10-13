#include "ui/UiScreenSlot.h"

#include <ui/UiService.h>

#include <utility>

UiScreenSlot::UiScreenSlot(UiService& ui, std::function<UiScreenDesc()> describe, UiSurfaceId surface)
    : Ui(ui)
    , Describe(std::move(describe))
    , Where(surface)
{
}

UiScreenSlot::~UiScreenSlot()
{
    Close();
}

void UiScreenSlot::MoveTo(UiSurfaceId surface)
{
    if (surface == Where)
        return;
    const bool open = IsOpen();
    Close();
    Where = surface;
    if (open)
        (void)Open();
}

bool UiScreenSlot::Open()
{
    if (!Screen.IsValid() && Where.IsValid())
        Screen = Ui.OpenScreen(Where, Describe());
    return Screen.IsValid();
}

void UiScreenSlot::Close()
{
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}
