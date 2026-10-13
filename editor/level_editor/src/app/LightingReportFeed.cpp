#include "LightingReportFeed.h"

#include "EditorCookRuntime.h"
#include "render/EditorRenderFeature.h"

LightingReports LightingReportsFrom(EditorRenderFeature* feature, EditorCookRuntime& cook)
{
    const auto builder = [feature]() -> SceneRenderQueueBuilder* {
        return feature != nullptr ? feature->FocusQueueBuilder() : nullptr;
    };
    return {
        .Shadows = feature != nullptr ? &feature->ShadowReadout() : nullptr,
        .Preview = [builder, &cook] {
            const SceneRenderQueueBuilder* queue = builder();
            if (queue == nullptr || cook.LastRecord() == nullptr)
                return BakedLightingPreview::Unavailable;
            if (!queue->LightmapPreviewEnabled() || !queue->LightmapPreviewLoaded())
                return BakedLightingPreview::Off;
            return queue->LightmapPreviewStale() ? BakedLightingPreview::Stale : BakedLightingPreview::Fresh;
        },
        .ShowPreview = [builder, &cook](bool enabled) {
            SceneRenderQueueBuilder* queue = builder();
            if (queue == nullptr)
                return;
            if (enabled)
                cook.RefreshPreviewNow(*queue);
            queue->SetLightmapPreviewEnabled(enabled);
        },
        .RerenderShadows = [feature] {
            if (feature != nullptr)
                feature->InvalidateShadows();
        },
        .LastCook = [&cook]() -> std::optional<LightingCook> {
            const CookSession::Record* record = cook.LastRecord();
            if (record == nullptr)
                return std::nullopt;
            return LightingCook{ .ProbeVolumes = record->ProbeVolumeCount, .Probes = record->ProbeCount };
        },
    };
}
