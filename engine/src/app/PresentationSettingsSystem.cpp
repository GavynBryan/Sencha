#include <app/PresentationSettingsSystem.h>

#include <app/GameContexts.h>
#include <app/LoadedLevel.h>
#include <render/PresentationLayers.h>
#include <render/ZonePresentation.h>

PresentationSettingsSystem::PresentationSettingsSystem(const LoadedLevel& level, const RuntimeWorld& runtime,
                                                       RuntimeAssets& assets, const PostEffectCatalog& effects)
    : Level(level)
    , Runtime(runtime)
    , Game(assets)
    , Publisher(effects)
{
}

void PresentationSettingsSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    const ZoneId zone = Level.PlayZone();
    const Inputs inputs{ .Game = Game.Get(),
                         .World = Level.Manifest(),
                         .Zone = zone,
                         .Scene = FindZonePresentation(Runtime, zone) };
    if (Resolved == inputs)
        return;
    Resolved = inputs;

    const PresentationLayerStack layers = PresentationLayersAt(inputs.Game, inputs.World, inputs.Zone, inputs.Scene);
    Publisher.Publish(ctx.Entities, ResolvePresentationSettings(layers.Layers()));
}
