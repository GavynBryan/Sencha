#include <ui/UiService.h>

#include "UiRuntime.h"

UiService::UiService(LoggingProvider& logging,
                     AssetSystem& assets,
                     UiPackageCache& packages,
                     FontFaceCache& fonts)
    : Runtime(std::make_unique<UiRuntime>(logging, assets, packages, fonts))
{
}

UiService::~UiService() = default;

bool UiService::IsReady() const
{
    return Runtime->IsReady();
}

UiSurfaceId UiService::CreateSurface(std::string_view name, RenderExtent size)
{
    return Runtime->CreateSurface(name, size);
}

void UiService::DestroySurface(UiSurfaceId surface)
{
    Runtime->DestroySurface(surface);
}

void UiService::SetSurfaceSize(UiSurfaceId surface, RenderExtent size)
{
    Runtime->SetSurfaceSize(surface, size);
}

RenderExtent UiService::GetSurfaceSize(UiSurfaceId surface) const
{
    return Runtime->GetSurfaceSize(surface);
}

UiScreenHandle UiService::OpenScreen(UiSurfaceId surface, std::string_view packagePath)
{
    return Runtime->OpenScreen(surface, packagePath);
}

void UiService::CloseScreen(UiScreenHandle screen)
{
    Runtime->CloseScreen(screen);
}

bool UiService::IsScreenOpen(UiScreenHandle screen) const
{
    return Runtime->IsScreenOpen(screen);
}

void UiService::Update()
{
    Runtime->Update();
}

std::optional<UiElementBox> UiService::MeasureElement(UiScreenHandle screen,
                                                      std::string_view elementId) const
{
    return Runtime->MeasureElement(screen, elementId);
}
