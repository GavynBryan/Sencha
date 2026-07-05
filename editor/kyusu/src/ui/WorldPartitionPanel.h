#pragma once

#include "ui/IEditorPanel.h"

#include <ecs/EntityId.h>
#include <zone/WorldPartitionIds.h>
#include <zone/ZoneId.h>

class CommandStack;
class SelectionService;
class WorldDocument;
struct ContentRiskRecord;
struct RegionRecord;
struct TransitionRecord;
struct ZoneHeader;

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
    void DrawZoneRow(const ZoneHeader& zone);
    void DrawTransitionRow(const TransitionRecord& transition);
    // The portal connect strip between the tree and the validation list: shown
    // while the selection is exactly one portal entity; connects unlinked
    // portals (target pre-guessed from facing) and navigates linked ones.
    void DrawConnectBar();
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
    EntityId PendingConnectPortal_;
    // Connect-bar transients: the target re-guesses when the selected portal
    // changes, then stays a plain user choice.
    EntityId ConnectBarEntity_;
    ZoneId   ConnectBarTarget_;
    bool     ConnectBarTwoWay_ = true;
};
