#pragma once

#include "brush/BrushMesh.h"
#include "selection/SelectableRef.h"

#include <ecs/EntityId.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

class CommandStack;
struct ManipulationSink;

// Face inset and edge bevel over the current selection. Both capture the
// pre-edit meshes, preview a regenerated result as their parameters change, and
// resolve through one commit or one cancel, so they are one machine rather than
// two: only the captured elements and the operation applied to them differ.
//
// Pending exactly while the captures hold the pre-edit meshes and the command
// stack's pending-edit scope is open. Only BeginInset and BeginBevel enter that
// state, and only Commit and Cancel leave it.
//
// The target is captured at begin. Rebuilding the editing stack cancels any
// pending edit before replacing the sink, so the captured target cannot outlive
// the edit that holds it.
class PendingElementEdit
{
public:
    // Insets every selected face, previewing at `distance`. Does nothing when
    // no selected face resolves against the target.
    void BeginInset(ManipulationSink& target, CommandStack& commands,
                    std::span<const SelectableRef> selection, float distance);

    // Bevels every selected edge, previewing at `width` with `segments` clamped
    // to [1, 16]. Does nothing when no selected edge resolves.
    void BeginBevel(ManipulationSink& target, CommandStack& commands,
                    std::span<const SelectableRef> selection, float width, int segments);

    [[nodiscard]] bool HasPendingInset() const { return Stage == Phase::Inset; }
    [[nodiscard]] bool HasPendingBevel() const { return Stage == Phase::Bevel; }

    void SetInsetDistance(float distance);
    void SetBevelParams(float width, int segments);

    // Commits every capture whose result actually changed the mesh, as one
    // undoable step. Entities that died while pending are dropped.
    void Commit(CommandStack& commands);

    // Restores the captured meshes and closes the scope.
    void Cancel(CommandStack& commands);

private:
    enum class Phase : std::uint8_t
    {
        Idle,
        Inset,
        Bevel,
    };

    // One entity's pre-edit mesh and the elements the operation applies to.
    // Faces index the original's faces (inset); Edges are vertex pairs into the
    // original (bevel), since edge indices do not survive regeneration.
    struct Capture
    {
        EntityId Entity = {};
        BrushMesh Original;
        std::vector<std::uint32_t> Faces;
        std::vector<std::array<std::uint32_t, 2>> Edges;
    };

    [[nodiscard]] static BrushMesh BuildEdited(Phase stage, const Capture& capture,
                                               float distance, float width, int segments);

    void Regenerate();
    void ClearCapture();

    Phase Stage = Phase::Idle;
    ManipulationSink* Target = nullptr;
    std::vector<Capture> Captures;
    float Distance = 0.0f; // Inset
    float Width = 0.0f;    // Bevel
    int Segments = 1;      // Bevel
};
