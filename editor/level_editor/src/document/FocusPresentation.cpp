#include "document/FocusPresentation.h"

#include "document/WorldDocument.h"

#include <render/PresentationLayers.h>

PresentationSettings FocusPresentation(const WorldDocument& world, const PresentationSettingsLayer* game)
{
    const PresentationSettingsLayer& focus = world.FocusDocument().GetPresentation();
    if (!world.IsWorld())
        return ResolvePresentationSettings(PresentationLayersAt(game, nullptr, {}, &focus).Layers());
    const bool zone = !world.IsWorldSceneFocused();
    return ResolvePresentationSettings(
        PresentationLayersAt(game, &world.Manifest(), zone ? world.FocusZone() : ZoneId{}, zone ? &focus : nullptr)
            .Layers());
}
