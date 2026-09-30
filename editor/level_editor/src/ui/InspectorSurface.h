#pragma once

#include "AssetFieldCandidates.h"

#include <core/assets/AssetRef.h>
#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstddef>
#include <string>
#include <vector>

class CommandStack;
class EditorComponentAdapterRegistry;
class SelectionService;
class UiService;
class WorldDocument;
struct IComponentSerializer;

//=============================================================================
// InspectorSurface
//
// The selected entity's components as an authored document: one row per schema
// leaf, published from the live bytes and read back when the document says an
// edit is finished.
//
// Registry-driven, exactly like the ImGui inspector it sits over. It walks the
// component serializer registry and presents each present component's
// RuntimeFields(), so any component -- engine or game-module -- is shown and
// edited without this naming its type. What changes is where the widgets live:
// the document decides how a row looks, this decides what a row means.
//
// The edit transaction is the interesting part, and it is spelled out in the
// document's vocabulary rather than in focus state this would have to mirror:
//
//   inspector_begin(row)   a field took focus. Republishing stops for that row,
//                          because refilling it every frame would delete what
//                          somebody is typing between keystrokes.
//   inspector_commit(row)  the field is done. The text is parsed, and a value
//                          that changed becomes one RawComponentEditCommand on
//                          the editor's own stack -- undoable, and identical to
//                          what the ImGui inspector commits.
//
// Every interruption ends the transaction with nothing to roll back: the text
// lived in the screen's presentation copy and never reached the component, so
// closing the surface, changing the selection, or losing the entity discards it.
//
// An asset field is never text. It is refcounted and session-local, so it
// travels through AssetFieldEditCommand rather than a byte write, and it is
// chosen from what the project actually holds. Clicking one opens a picker
// inside the same document: the host publishes the candidates it scanned, the
// document lists them, and choosing one raises an action naming an index. No
// path is ever typed, and the document never learns what an asset is.
//
// A component whose adapter authors its own inspector rows -- the gameplay
// vocabularies, the world dock, brush modifiers -- gets one row saying where it
// is edited, not its raw schema. Those adapters replace the generic rows in the
// panel too, so showing raw zone ids here would be two inspectors contradicting
// each other about the same component. Presenting them properly is per-adapter
// work each will have to earn; saying so is what this owes in the meantime.
//
// A list-arity asset field shows and picks per slot, but slots cannot be added
// or removed from here.
//
// The ImGui inspector stays. This coexists with it until it is demonstrably
// better, which is the only honest way to find out.
//=============================================================================
class InspectorSurface
{
public:
    // The surface is the host's: authored screens arbitrate focus and modality
    // within one, so a surface per controller would mean a modal that takes
    // focus from nothing.
    InspectorSurface(UiService& ui,
                     UiSurfaceId surface,
                     WorldDocument& world,
                     SelectionService& selection,
                     CommandStack& commands,
                     const EditorComponentAdapterRegistry& adapters);

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const { return Screen.IsValid(); }

    // The open screen, for a caller that wants to read what is being presented
    // -- a test asserting the rows followed the selection, or a host wiring a
    // second surface against the same document.
    [[nodiscard]] UiScreenHandle CurrentScreen() const { return Screen; }

    // Per frame, before the engine updates the UI: act on what the document
    // asked for, then publish what it should now show.
    void Update();

private:
    // Positional ids, declared beside the description so the two cannot drift.
    enum class Property : std::size_t { EntityLabel = 0, Status, HasSelection,
                                        PickerOpen, PickerLabel };
    enum class Action : std::size_t { Begin = 0, Commit, Close, Pick, Choose, CancelPick };

    [[nodiscard]] static UiScreenDesc Describe();

    // Where a published row came from. Parallel to the row list by index,
    // because the index is the only thing the document hands back -- and
    // because a row carrying a component id would be state the presentation
    // layer has no use for and no business holding.
    struct RowOrigin
    {
        const IComponentSerializer* Serializer = nullptr;
        ComponentId Component = InvalidComponentId;
        std::size_t Field = 0;
        // Which slot of a list-arity asset field this row stands for. Zero for
        // everything else, which is also the only slot a single-arity field has.
        std::size_t Slot = 0;
        // An asset handle is chosen, never typed, so a click on this row opens
        // a picker instead of doing nothing.
        bool Pickable = false;
        // The text this row was last published with. A commit compares against
        // it rather than against the component, so a field somebody focused and
        // left alone cannot become an edit through the rounding in its own
        // display.
        std::string Published{};
        // What the picker calls itself when opened from this row -- the field's
        // label, plus the slot for a list. Kept here because by the time a
        // picker is open the row it came from is the only thing that knows.
        std::string PickerLabel{};
    };

    void Publish();
    void HandleBegin(std::size_t row);
    void HandleCommit(std::size_t row);
    // Opens the picker for an asset row, scanning the project once for what the
    // field accepts. A row that is not an asset field does nothing: what a click
    // means is the host's decision, so the document offers one on every
    // read-only row and this refuses the ones it has no picker for.
    void HandlePick(std::size_t row);
    void HandleChoose(std::size_t option);
    void ClosePicker();
    void PublishPicker();

    // One asset field as rows: one for a single handle, one per slot for a
    // list. Separate from the scalar path because an asset reference is read
    // through the asset system rather than off an offset, and because a list
    // arity is several rows where every other field is one.
    void AppendAssetRows(const IComponentSerializer& serializer,
                         ComponentId component,
                         std::size_t fieldIndex,
                         const struct RuntimeField& field,
                         const void* bytes,
                         const std::string& componentKey,
                         std::vector<UiRow>& rows,
                         std::vector<RowOrigin>& origins) const;
    // Ends the edit transaction. Nothing to undo: the text never left the
    // presentation copy.
    void AbandonEdit();

    [[nodiscard]] EntityId SelectedEntity() const;
    // What the title bar says: the authored name, or the handle when there is
    // none to say.
    [[nodiscard]] std::string EntityLabel(EntityId entity) const;

    UiService& Ui;
    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;
    const EditorComponentAdapterRegistry& Adapters;

    UiSurfaceId Surface;
    UiScreenHandle Screen;

    std::vector<RowOrigin> Origins;
    EntityId Showing = {};
    std::string Status;

    // The open picker: which row asked for it, and what it is offering. The
    // candidates are scanned when it opens rather than every frame, because
    // narrowing a structured-data field reads each candidate's envelope off
    // disk. Index zero of the published list is "(none)"; the rest line up with
    // PickerCandidates.
    static constexpr std::size_t kNoRow = static_cast<std::size_t>(-1);
    std::size_t PickingRow = kNoRow;
    std::vector<AssetFieldCandidate> PickerCandidates;

    // The row currently being edited, or none. While one is held the row list
    // is not republished, which is what keeps a republish from overwriting what
    // is being typed.
    std::size_t EditingRow = kNoRow;
};
