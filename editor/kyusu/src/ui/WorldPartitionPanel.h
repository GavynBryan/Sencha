#pragma once

#include "ui/IEditorPanel.h"

#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneId.h>

class CommandStack;
class SelectionService;
class WorldDocument;
struct ContentRiskRecord;

// Derived label for a region's streaming-shape combo. The shape is read off
// the radius in force: positive = Proximity (cells load by distance), zero =
// Graph (rooms load through authored connections); an absent override shows
// the base's shape marked inherited. Presentation only, computed from the
// values; nothing stores a mode.
[[nodiscard]] inline const char* RegionStreamingShapeLabel(const RegionStreamingConfig& streaming,
                                                           double baseRadius)
{
    const bool proximity = streaming.Radius.value_or(baseRadius) > 0.0;
    if (!streaming.Radius.has_value())
        return proximity ? "Inherited (Proximity)" : "Inherited (Graph)";
    return proximity ? "Proximity" : "Graph";
}

// The partition tree: regions containing zone rows in manifest order, with the
// per-zone state (focus/context/hidden/header-only), open/visible controls, and
// the stored validation records. Every mutation routes through the WorldDocument
// verbs; the panel owns nothing beyond ImGui transients (the inline-rename
// buffer and the validation-click navigation target). Draws nothing in legacy
// mode: the partition vocabulary only exists for manifest-backed worlds.
class WorldPartitionPanel : public IEditorPanel
{
public:
    WorldPartitionPanel(WorldDocument& world, SelectionService& selection,
                        CommandStack& commands);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

private:
    void DrawHeaderButtons();
    void DrawRegion(const RegionRecord& region);
    // The region's streaming shape: derived badge plus inline hop/radius/cap
    // editors, each clearable back to inherited (the manifest's absent state).
    void DrawRegionStreaming(const RegionRecord& region);
    void DrawZoneRow(const ZoneHeader& zone);
    // The world-level connection list: one row per symmetric pair (or per
    // one-way edge), never nested under zones, because connections are world
    // data. Every edit applies to both directions of a pair.
    void DrawConnections();
    void DrawConnectionRow(TransitionId representative, TransitionId partner);
    // Live demand list from the pure streaming policy around the preview
    // focus the viewport resolved; the bounds tint in the viewport and this
    // list read the same computation.
    void DrawStreamingPreview();
    void DrawValidation();
    void NavigateToRecord(const ContentRiskRecord& record);
    // Inline-rename helper: draws the InputText when `active`, commits through
    // the matching verb on deactivate. Returns true while the rename row is up.
    bool DrawRenameField(bool active);

    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;

    // ImGui transients only (never document state).
    ZoneId   RenamingZone_;
    RegionId RenamingRegion_;
    TransitionId RenamingTransition_;
    char     RenameBuffer_[128] = {};
    RegionId NavigateRegion_;   // region to force-open after a validation click
    ZoneId   SelectedZoneRow_;  // zone row a validation click highlighted
    TransitionId SelectedTransitionRow_; // transition row a validation click highlighted
    // Deferred Connect To request: executed at the top of the next draw,
    // because minting a zone mid-iteration would invalidate the manifest
    // loops the tree walks. Invalid To plus a valid NewRegion mints the zone.
    ZoneId   PendingConnectFrom_;
    ZoneId   PendingConnectTo_;
    RegionId PendingConnectNewRegion_;
};
