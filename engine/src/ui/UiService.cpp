#include <ui/UiService.h>

#include "UiRuntime.h"

UiService::UiService(LoggingProvider& logging,
                     AssetSystem& assets,
                     UiPackageCache& packages,
                     FontFaceCache& fonts,
                     TextureCache* textures,
                     SDL_Window* window)
    : Runtime(std::make_unique<UiRuntime>(logging, assets, packages, fonts, textures, window))
{
}

UiService::~UiService() = default;

void UiService::Shutdown()
{
    Runtime->Shutdown();
}

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

void UiService::SetSurfaceScale(UiSurfaceId surface, float scale)
{
    Runtime->SetSurfaceScale(surface, scale);
}

float UiService::GetSurfaceScale(UiSurfaceId surface) const
{
    return Runtime->GetSurfaceScale(surface);
}

UiScreenHandle UiService::OpenScreen(UiSurfaceId surface, const UiScreenDesc& desc)
{
    return Runtime->OpenScreen(surface, desc);
}

UiScreenHandle UiService::OpenScreen(UiSurfaceId surface, std::string_view packagePath)
{
    UiScreenDesc desc;
    desc.PackagePath = std::string(packagePath);
    return Runtime->OpenScreen(surface, desc);
}

bool UiService::SetValue(UiScreenHandle screen, UiModelPropertyId property, UiValue value)
{
    return Runtime->SetValue(screen, property, std::move(value));
}

bool UiService::SetArray(UiScreenHandle screen, UiModelArrayId array,
                         std::span<const std::string> items)
{
    return Runtime->SetArray(screen, array, items);
}

bool UiService::SetRows(UiScreenHandle screen, UiModelRowsId rows,
                        std::span<const UiRow> items)
{
    return Runtime->SetRows(screen, rows, items);
}

std::vector<UiRow> UiService::GetRows(UiScreenHandle screen, UiModelRowsId rows) const
{
    return Runtime->GetRows(screen, rows);
}

UiModelRowsId UiService::FindRows(UiScreenHandle screen, std::string_view path) const
{
    return Runtime->FindRows(screen, path);
}

std::size_t UiService::ArraySize(UiScreenHandle screen, UiModelArrayId array) const
{
    return Runtime->ArraySize(screen, array);
}

UiModelArrayId UiService::FindArray(UiScreenHandle screen, std::string_view path) const
{
    return Runtime->FindArray(screen, path);
}

UiValue UiService::GetValue(UiScreenHandle screen, UiModelPropertyId property) const
{
    return Runtime->GetValue(screen, property);
}

UiModelPropertyId UiService::FindProperty(UiScreenHandle screen, std::string_view path) const
{
    return Runtime->FindProperty(screen, path);
}

UiActionId UiService::FindAction(UiScreenHandle screen, std::string_view name) const
{
    return Runtime->FindAction(screen, name);
}

std::vector<UiAction> UiService::DrainActions()
{
    return Runtime->DrainActions();
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

bool UiService::ProcessPlatformEvent(const SDL_Event& event)
{
    return Runtime->ProcessPlatformEvent(event);
}

UiInputCapture UiService::Capture() const
{
    return Runtime->Capture();
}

void UiService::Navigate(UiSurfaceId surface, UiNavigation direction)
{
    Runtime->Navigate(surface, direction);
}

void UiService::ExtractRender()
{
    Runtime->ExtractRender();
}

const std::vector<UiDrawFrame>& UiService::Frames() const
{
    return Runtime->Frames();
}

std::optional<UiElementBox> UiService::MeasureElement(UiScreenHandle screen,
                                                      std::string_view elementId) const
{
    return Runtime->MeasureElement(screen, elementId);
}
