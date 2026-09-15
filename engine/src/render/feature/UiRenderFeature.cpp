#include <render/feature/UiRenderFeature.h>

#include <profiling/RenderStats.h>

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

    Instrumentation = services.Instrumentation;

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

    RenderStats* stats =
        Instrumentation != nullptr ? Instrumentation->Stats : nullptr;

    // One pass per surface, in the order extraction recorded them. The pass
    // reports its own frame, so the totals are summed here: what a host wants
    // to know is what the authored UI cost, not what one surface of it did.
    for (const UiDrawFrame& ui : Ui.Frames())
    {
        Pass.Draw(*frame.Backend, ui);
        if (stats == nullptr)
            continue;
        const UiDrawPass::DrawStats& drawn = Pass.GetLastDrawStats();
        stats->UiDrawCalls += drawn.DrawCalls;
        stats->UiTriangles += drawn.Triangles;
        stats->UiTextureUploads += drawn.TextureUploads;
        stats->UiTextureUploadBytes += drawn.TextureUploadBytes;
    }
}

void UiRenderFeature::Teardown()
{
    Pass.Teardown();
    Ready = false;
}
