#include "render/ViewportPostEffects.h"

#include <render/PresentationSettingsPublisher.h>
#include <render/post/EnginePostEffects.h>
#include <render/post/PostEffectFrame.h>

#include <algorithm>

bool ViewportPostEffects::Setup(const RendererServices& services)
{
    Services = services;
    RegisterEnginePostEffects(Catalog);
    Bloom = Catalog.Find("sencha.bloom");
    Ready = Bloom.IsValid();
    return Ready;
}

void ViewportPostEffects::Teardown()
{
    for (Viewport& viewport : Chains)
        viewport.Chain->Teardown();
    for (Retiring& retiring : Retired)
        retiring.Chain->Teardown();
    Chains.clear();
    Retired.clear();
    Ready = false;
}

void ViewportPostEffects::Record(const FrameContext& frame, ViewportId viewport, const PresentationSettings& settings,
                                 const OutputTransformParams& output)
{
    if (!Ready)
        return;
    auto found = std::ranges::find(Chains, viewport, &Viewport::Id);
    if (found == Chains.end())
    {
        if (!settings.Bloom.Enabled)
            return;
        auto chain = std::make_unique<PostEffectChain>();
        if (!chain->Setup(Services, Catalog))
        {
            chain->Teardown();
            return;
        }
        found = Chains.insert(Chains.end(), { viewport, std::move(chain) });
    }

    PostEffectFrame effects;
    if (settings.Bloom.Enabled)
        effects.Entries.push_back({ .Effect = Bloom,
                                    .Domain = PostEffectDomain::Scene,
                                    .Weight = 1.0f,
                                    .Params = BloomEffectParams(settings.Bloom) });
    found->Chain->Record(frame, effects, output);
}

void ViewportPostEffects::Prune(std::span<const ViewportId> live, const GpuFrameRetirement& retirement)
{
    for (auto viewport = Chains.begin(); viewport != Chains.end();)
    {
        if (std::ranges::find(live, viewport->Id) != live.end())
        {
            ++viewport;
            continue;
        }
        Retired.push_back({ std::move(viewport->Chain), retirement.Stamp() });
        viewport = Chains.erase(viewport);
    }
    std::erase_if(Retired, [&](Retiring& retiring) {
        if (!retirement.IsRetired(retiring.Stamp))
            return false;
        retiring.Chain->Teardown();
        return true;
    });
}
