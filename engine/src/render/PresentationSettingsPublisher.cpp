#include <render/PresentationSettingsPublisher.h>

#include <render/post/PostEffectContributions.h>

std::array<float, kPostEffectParamCount> BloomEffectParams(const BloomSettings& bloom)
{
    return { bloom.Threshold, bloom.Intensity, bloom.Spread };
}

PresentationSettingsPublisher::PresentationSettingsPublisher(const PostEffectCatalog& effects)
    : Bloom(effects.Find("sencha.bloom"))
{
}

void PresentationSettingsPublisher::Publish(World& world, const PresentationSettings& settings)
{
    if (Published == settings)
        return;
    Published = settings;
    if (!Writer.IsValid())
        Writer = RegisterPresentationWriter(world, "sencha.presentation_settings");
    if (!Bloom.IsValid())
        return;

    const PresentationOwner owner{ .Writer = Writer, .Holder = {} };
    if (!settings.Bloom.Enabled)
    {
        ClearPostEffect(world, Bloom, owner);
        return;
    }
    PostEffectContribution bloom;
    bloom.Owner = owner;
    bloom.Order = kPresentationSettingsOrder;
    bloom.Weight = 1.0f;
    bloom.Params = BloomEffectParams(settings.Bloom);
    bloom.Lifetime = PostEffectLifetime::Session;
    SetPostEffect(world, Bloom, bloom);
}
