#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstddef>
#include <string>
#include <vector>

class CommandStack;
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
// What it does not do yet: a component that registers an EditorComponentAdapter
// (the gameplay vocabularies, the world dock, brush modifiers) draws its own
// rows in the ImGui panel, and here it falls back to its raw schema -- truthful,
// but a tag id where the panel offers a picker. Asset handles are shown and not
// offered for the same reason: they are refcounted and session-local, so they
// need a picker and the command that goes with one rather than a byte write.
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
                     CommandStack& commands);

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
    enum class Property : std::size_t { EntityLabel = 0, Status, HasSelection };
    enum class Action : std::size_t { Begin = 0, Commit, Close };

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
        // The text this row was last published with. A commit compares against
        // it rather than against the component, so a field somebody focused and
        // left alone cannot become an edit through the rounding in its own
        // display.
        std::string Published;
    };

    void Publish();
    void HandleBegin(std::size_t row);
    void HandleCommit(std::size_t row);
    // Ends the edit transaction. Nothing to undo: the text never left the
    // presentation copy.
    void AbandonEdit();

    [[nodiscard]] EntityId SelectedEntity() const;

    UiService& Ui;
    WorldDocument& WorldDoc;
    SelectionService& Selection;
    CommandStack& Commands;

    UiSurfaceId Surface;
    UiScreenHandle Screen;

    std::vector<RowOrigin> Origins;
    EntityId Showing = {};
    std::string Status;

    // The row currently being edited, or none. While one is held the row list
    // is not republished, which is what keeps a republish from overwriting what
    // is being typed.
    static constexpr std::size_t kNoRow = static_cast<std::size_t>(-1);
    std::size_t EditingRow = kNoRow;
};
