#pragma once

#include "ui/IEditorPanel.h"

#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/ZoneId.h>

#include <string>

class CommandStack;
class SelectionService;
class WorldDocument;
class EditorEntityRecipeRegistry;
struct ContentRiskRecord;

// Derived label for a graph's streaming-shape combo. The shape is read off
// the radius in force: positive = Proximity (cells load by distance), zero =
// Graph (rooms load through authored connections); an absent override shows
// the base's shape marked inherited. Presentation only, computed from the
// values; nothing stores a mode.
[[nodiscard]] inline const char* GraphStreamingShapeLabel(const GraphStreamingConfig& streaming,
                                                           double baseRadius)
{
    const bool proximity = streaming.Radius.value_or(baseRadius) > 0.0;
    if (!streaming.Radius.has_value())
        return proximity ? "Inherited (Proximity)" : "Inherited (Graph)";
    return proximity ? "Proximity" : "Graph";
}

// The partition tree: graphs containing zone rows in manifest order, with the
// per-zone state (focus/context/hidden/header-only), open/visible controls, and
// the stored validation records. Every mutation routes through the WorldDocument
// verbs; the panel owns nothing beyond ImGui transients (the inline-rename
// buffer and the validation-click navigation target). Draws nothing in legacy
// mode: the partition vocabulary only exists for manifest-backed worlds.
class WorldPartitionPanel : public IEditorPanel
{
public:
    WorldPartitionPanel(WorldDocument& world, SelectionService& selection,
                        CommandStack& commands, EditorEntityRecipeRegistry& recipes);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Right; }
    // Wider share of the upper-right row than the hierarchy packed beside it.
    float GetDockWeight() const override { return 1.45f; }

private:
    void DrawHeaderButtons();
    // The world row above the graphs: the world scene, focusable like a zone.
    // It shows the world's name (the scene has none of its own) and carries no
    // bounds badge and no eye toggle; it is always present, never streamed.
    void DrawWorldSceneRow();
    void DrawGraph(const GraphRecord& graph);
    // The graph's streaming shape: derived badge plus inline hop/radius/cap
    // editors, each clearable back to inherited (the manifest's absent state).
    void DrawGraphStreaming(const GraphRecord& graph);
    void DrawZoneRow(const ZoneHeader& zone);
    void DrawLegacyTransitionMigration();
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
    EditorEntityRecipeRegistry& Recipes;

    // ImGui transients only (never document state).
    ZoneId   RenamingZone_;
    GraphId RenamingGraph_;
    char     RenameBuffer_[128] = {};
    GraphId NavigateGraph_;   // graph to force-open after a validation click
    ZoneId   SelectedZoneRow_;  // zone row a validation click highlighted
    std::string MigrationSummary_;
};
