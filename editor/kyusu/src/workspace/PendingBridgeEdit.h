#pragma once

#include "brush/BrushOps.h"
#include "brush/FaceMaterial.h"
#include "selection/SelectableRef.h"

#include <ecs/EntityId.h>

#include <cstdint>
#include <span>
#include <vector>

class CommandStack;
class EditorDocument;
class EditorScene;
class SelectionService;

// Bridging two edge paths that live on different brushes. The result is a new
// brush rather than an edit of either source, so it is staged as a live preview
// entity the segment count can be tuned against before it becomes a document
// change.
//
// Pending exactly while a preview entity is live in the scene it was begun in
// and the command stack's pending-edit scope is open for it. Only Begin enters
// that state, and only Commit and Cancel leave it.
//
// The scene and document are captured at Begin: a focus change swaps the
// workspace's active document, and the preview must be committed or destroyed
// where it was created.
class PendingBridgeEdit
{
public:
    // True when the selected edges span exactly two brushes. Edges on one brush
    // are an immediate mesh edit instead, and are not this mechanism's work.
    [[nodiscard]] static bool SpansTwoBrushes(std::span<const SelectableRef> selection);

    // Stages the preview and opens the pending-edit scope. Does nothing when
    // the two edge paths cannot be paired: unequal lengths, one open and one
    // closed, a selection that does not resolve into a single path, or a bridge
    // mesh that comes back empty.
    void Begin(EditorScene& scene, EditorDocument& document, CommandStack& commands,
               std::span<const SelectableRef> selection, int segments);

    [[nodiscard]] bool HasPending() const { return Stage == Phase::Pending; }

    // Rebuilds the preview at a new segment count, clamped to [1, 64].
    void SetSegments(int segments);

    // Turns the preview into an undoable creation. Falls back to Cancel when
    // the preview entity did not survive.
    void Commit(CommandStack& commands, SelectionService& selection);

    // Destroys the preview and closes the scope.
    void Cancel(CommandStack& commands);

private:
    void Regenerate();
    void ClearCapture();

    enum class Phase : std::uint8_t
    {
        Idle,
        Pending,
    };

    Phase Stage = Phase::Idle;

    EntityId Entity = {};
    BrushOps::BridgePathSpec PathA;
    BrushOps::BridgePathSpec PathB;
    FaceMaterial Material;
    int Segments = 1;
    EditorScene* Scene = nullptr;
    EditorDocument* Document = nullptr;
    // The selection to restore when the staged creation is undone.
    std::vector<SelectableRef> BeforeSelection;
};
