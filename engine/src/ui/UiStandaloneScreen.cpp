#include <ui/UiStandaloneScreen.h>

#include <ui/UiService.h>

bool UiStandaloneScreen::OpenOverWindow(UiService* ui, std::string_view surfaceName, const UiScreenDesc& desc,
                                        UiSurfaceInputPolicy input, UiStackOrder order)
{
    if (Attempted)
        return IsOpen();
    if (ui == nullptr || !ui->IsReady() || !ui->GetWindowView().has_value())
        return false;
    const UiSurfaceId surface = ui->CreateWindowSurface(surfaceName, order);
    ui->SetSurfaceInputPolicy(surface, input);
    OpenOn(*ui, surface, desc);
    return IsOpen();
}

bool UiStandaloneScreen::OpenInTexture(UiService* ui, std::string_view surfaceName, const UiScreenDesc& desc,
                                       RenderExtent extent, std::string_view texture)
{
    if (Attempted)
        return IsOpen();
    if (ui == nullptr || !ui->IsReady())
        return false;
    const UiSurfaceId surface = ui->CreateSurface(surfaceName, extent);
    ui->SetSurfaceTexture(surface, texture);
    OpenOn(*ui, surface, desc);
    return IsOpen();
}

void UiStandaloneScreen::OpenOn(UiService& ui, UiSurfaceId surface, const UiScreenDesc& desc)
{
    Attempted = true;
    Service = &ui;
    OwnSurface = surface;
    Opened = ui.OpenScreen(surface, desc);
    if (!Opened.IsValid())
    {
        ui.DestroySurface(OwnSurface);
        OwnSurface = {};
    }
}

void UiStandaloneScreen::Close()
{
    if (Service != nullptr && OwnSurface.IsValid())
        Service->DestroySurface(OwnSurface);
    Service = nullptr;
    OwnSurface = {};
    Opened = {};
    Attempted = false;
}
