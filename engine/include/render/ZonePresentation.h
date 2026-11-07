#pragma once

#include <render/PresentationSettings.h>
#include <zone/ZoneId.h>

class RuntimeWorld;
struct RuntimeZoneRecord;

// A zone's scene layer, held on its record so it goes when the zone does.
struct ZonePresentation
{
    PresentationSettingsLayer Layer;
};

// Holds nothing for an empty layer.
void AttachZonePresentation(RuntimeZoneRecord& zone, const PresentationSettingsLayer& layer);

// Null unless the zone is resident and its scene authored a layer.
[[nodiscard]] const PresentationSettingsLayer* FindZonePresentation(const RuntimeWorld& runtime, ZoneId zone);
