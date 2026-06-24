#pragma once

#include "EditorLinePipeline.h"

#include "../editmodes/IManipulator.h"
#include "../document/EditorScene.h"
#include "../brush/BrushMesh.h"
#include "../meshedit/MeshElements.h"
#include "../selection/SelectionService.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

#include <vector>

class ManipulatorSession;
class MeshEditService;
struct EditorOverlayState;

// Draws selection highlights (object/face/edge/vertex), the hovered element glow,
// and the active manipulators via the shared editor line pipeline. Gathers lines only.
class SelectionRenderer
{
public:
    SelectionRenderer(EditorScene& scene, SelectionService& selection, MeshEditService& meshEdit,
                      const EditorOverlayState& overlay, ManipulatorSession& session, EditorLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport);

private:
    // Every edge of the mesh, for the selected-mesh wireframe overlay.
    void AppendWireframe(std::vector<EditorLineVertex>& vertices,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color) const;
    void AppendFace(std::vector<EditorLineVertex>& vertices,
                    const FaceElement& face,
                    const Vec4& color) const;
    void AppendEdge(std::vector<EditorLineVertex>& vertices,
                    const EdgeElement& edge,
                    const Vec4& color) const;
    // A screen-constant square at a vertex (the visible vertex handle).
    void AppendVertexSquare(std::vector<EditorLineVertex>& vertices,
                            const EditorViewport& viewport,
                            Vec3d position,
                            const Vec4& color) const;
    // Entities referenced by the current selection (as objects or via element refs):
    // selecting any element makes the whole mesh the active body.
    [[nodiscard]] std::vector<EntityId> GatherActiveBodies() const;
    void AppendHover(std::vector<EditorLineVertex>& vertices, const EditorViewport& viewport) const;
    void AppendManipulators(std::vector<EditorLineVertex>& vertices,
                            const EditorViewport& viewport) const;

    EditorScene& Scene;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    const EditorOverlayState& Overlay;
    ManipulatorSession& Session;
    EditorLinePipeline& Lines;
};
