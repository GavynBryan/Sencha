#pragma once

#include "IEditorPanel.h"

#include "../brush/FaceMaterial.h"

#include <functional>
#include <optional>

class CommandStack;
class EditorDocument;
class MaterialLibrary;
class MeshEditService;
class SelectionService;
struct BrushFace;
struct BrushMesh;
struct IMeshEditTarget;

// Per-face texturing surface (04-§2). Applies a material and edits the UV
// projection (scale/offset/rotation, world/face alignment, justify) on the
// selected faces. Reads/commits through IMeshEditTarget exactly like the mesh
// verbs — it never touches BrushOps or the scene directly, so it stays generic
// over the edit target. Each edit is one undoable EditBrushMeshCommand.
class MaterialPanel : public IEditorPanel
{
public:
    MaterialPanel(IMeshEditTarget& target,
                  SelectionService& selection,
                  MeshEditService& meshEdit,
                  CommandStack& commands,
                  MaterialLibrary& materials,
                  EditorDocument& document);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Right; }

private:
    // Applies a mutation to every selected face, grouped into one undoable
    // command per brush. The mutation sees the whole mesh (for justify, which
    // needs the face geometry) and the face to edit.
    void EditSelectedFaces(const std::function<void(const BrushMesh&, BrushFace&)>& mutate);

    // The UV projection of the first selected face, by value — drives the
    // displayed values. Returned by value (not a pointer into the brush store):
    // applying a material rebuilds that mesh mid-frame, so a held pointer would
    // dangle.
    [[nodiscard]] std::optional<UvProjection> RepresentativeUv() const;

    void DrawMaterialPicker();
    void DrawUvControls(const UvProjection& current);

    IMeshEditTarget& Target;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    CommandStack& Commands;
    MaterialLibrary& Materials;
    EditorDocument& Document;

    // In-progress UV drag. A drag mutates this buffer (so it accumulates instead
    // of being reset to the scene value each frame) and commits one command when
    // it ends; the buffer is reseeded from the selection only while not editing.
    UvProjection UvEdit;
    bool         UvEditing = false;
};
