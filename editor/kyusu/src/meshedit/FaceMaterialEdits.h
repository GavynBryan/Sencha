#pragma once

#include "brush/FaceMaterial.h"

#include <functional>
#include <optional>
#include <vector>

class CommandStack;
class SelectionService;
struct BrushFace;
struct BrushMesh;
struct IMeshEditTarget;

//=============================================================================
// FaceMaterialEdits — selection-wide face-material edits, committed as one
// undoable command per brush. Every texturing verb (apply, justify, projection
// copy/paste) funnels through EditSelectedFaces so panels, shortcuts, and
// viewport gestures share one commit path. Reads/commits through
// IMeshEditTarget exactly like the mesh verbs: never through BrushOps or the
// scene directly.
//=============================================================================

// Applies a mutation to every selected face, grouped into one undoable command
// per brush. The mutation sees the whole mesh (justify needs the face
// geometry), the brush's world transform (for cross-brush UV work), and the
// face to edit.
void EditSelectedFaces(
    IMeshEditTarget& target,
    const SelectionService& selection,
    CommandStack& commands,
    const std::function<void(const BrushMesh&, const Transform3f&, BrushFace&)>& mutate);

// Sets the material on every selected face. An empty ref means "inherit the
// level default" (see EffectiveMaterial).
void ApplyMaterialToSelectedFaces(IMeshEditTarget& target,
                                  const SelectionService& selection,
                                  CommandStack& commands,
                                  const AssetRef& material);

// The UV projection of the first selected face, by value — drives displayed
// values. Returned by value (not a pointer into the brush store): applying a
// material rebuilds that mesh mid-frame, so a held pointer would dangle.
[[nodiscard]] std::optional<UvProjection> RepresentativeFaceUv(const IMeshEditTarget& target,
                                                               const SelectionService& selection);

// A face's material + projection expressed in world space, so it can be pasted
// onto faces of differently-transformed brushes and the texture lands
// identically.
struct FaceMaterialClipboard
{
    WorldUvProjection Projection;
    AssetRef Material;
};

// Stash the primary selected face's material + projection in world space.
// Empty when nothing face-like is selected.
[[nodiscard]] std::optional<FaceMaterialClipboard> CopySelectedFaceProjection(
    const IMeshEditTarget& target, const SelectionService& selection);

// Apply a stashed material + projection to every selected face, converting the
// world projection into each brush's local frame.
void PasteFaceProjection(IMeshEditTarget& target,
                         const SelectionService& selection,
                         CommandStack& commands,
                         const FaceMaterialClipboard& clipboard);

// Fit/Center the whole face selection as ONE unit: union world bounds drive a
// single world projection that every face then shares, so the texture flows
// continuously across faces and across brushes.
void JustifySelectedFacesAsOne(IMeshEditTarget& target,
                               const SelectionService& selection,
                               CommandStack& commands,
                               bool fit);

// A face's loop vertices in brush-local space — what the per-face UV justify
// presets operate on (the math itself lives in FaceMaterial, kernel-side).
[[nodiscard]] std::vector<Vec3d> FaceLocalPositions(const BrushMesh& mesh, const BrushFace& face);
