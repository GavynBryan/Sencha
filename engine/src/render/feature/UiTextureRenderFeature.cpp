#include <render/feature/UiTextureRenderFeature.h>

#include <core/logging/LoggingProvider.h>
#include <ui/UiService.h>

UiTextureRenderFeature::UiTextureRenderFeature(UiService& ui, TextureCache* textures, TargetTextureTable& targets)
    : Ui(ui)
    , Textures(textures)
    , Table(targets)
{
}

bool UiTextureRenderFeature::Setup(const RenderFeatureServices& services)
{
    if (services.Backend == nullptr)
        return false;
    Logger* log = services.Logging != nullptr ? &services.Logging->GetLogger<UiTextureRenderFeature>() : nullptr;
    Ready = Targets.Setup(*services.Backend, Textures, log);
    return Ready;
}

void UiTextureRenderFeature::OnDraw(const RenderFrame& frame)
{
    if (Ready)
        Targets.Record(frame, Ui.TextureFrames(), Table);
}

void UiTextureRenderFeature::Teardown()
{
    Targets.Teardown(Table);
    Ready = false;
}
