#include <render/feature/UiRenderFeature.h>

#include <ui/UiService.h>

UiRenderFeature::UiRenderFeature(UiService& ui, TextureCache* textures)
    : Ui(ui)
    , Textures(textures)
{
}

bool UiRenderFeature::Setup(const RenderFeatureServices& services)
{
    if (services.Backend == nullptr)
        return false;

    // False rather than degraded: unlike a shadow pass, there is no partial
    // authored UI worth presenting. A menu that half draws is worse than a
    // menu that visibly did not.
    Ready = Pass.Setup(*services.Backend, Textures);
    return Ready;
}

void UiRenderFeature::OnDraw(const RenderFrame& frame)
{
    if (!Ready || frame.Backend == nullptr)
        return;

    // One pass per surface, in the order extraction recorded them.
    for (const UiDrawFrame& ui : Ui.Frames())
        Pass.Draw(*frame.Backend, ui);
}

void UiRenderFeature::Teardown()
{
    Pass.Teardown();
    Ready = false;
}
