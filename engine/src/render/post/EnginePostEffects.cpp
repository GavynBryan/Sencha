#include <render/post/EnginePostEffects.h>

#include <render/post/PostEffectCatalog.h>
#include <shaders/kSenchaFadeFragSpv.h>

#include <cstdint>
#include <limits>
#include <span>

void RegisterEnginePostEffects(PostEffectCatalog& catalog)
{
    // Last among display effects, so a fade covers what they drew.
    (void)catalog.Register(PostEffectDesc{
        .Name = "sencha.fade",
        .FragmentSpirv = std::span<const std::uint32_t>(kSenchaFadeFragSpv, kSenchaFadeFragSpvWordCount),
        .Domain = PostEffectDomain::Display,
        .Order = std::numeric_limits<std::int16_t>::max(),
    });
}
