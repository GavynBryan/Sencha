#include <render/ZonePresentation.h>

#include <world/RuntimeWorld.h>

void AttachZonePresentation(RuntimeZoneRecord& zone, const PresentationSettingsLayer& layer)
{
    if (!layer.IsEmpty())
        zone.Resources.Register<ZonePresentation>(ZonePresentation{ layer });
}

const PresentationSettingsLayer* FindZonePresentation(const RuntimeWorld& runtime, ZoneId zone)
{
    const RuntimeZoneRecord* record = runtime.FindZone(zone);
    if (record == nullptr || record->State != RuntimeZoneLoadState::Resident)
        return nullptr;
    const ZonePresentation* presentation = record->Resources.TryGet<ZonePresentation>();
    return presentation != nullptr ? &presentation->Layer : nullptr;
}
