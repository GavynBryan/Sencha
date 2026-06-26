#pragma once

#include "EditorWideLinePipeline.h"

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
// and the active manipulators via the wide-line pipeline. Gathers segments only;
// the active body draws bolder than preview/hover so it reads as primary.
class SelectionRenderer
{
public:
    SelectionRenderer(EditorScene& scene, SelectionService& selection, MeshEditService& meshEdit,
                      const EditorOverlayState& overlay, ManipulatorSession& session,
                      EditorWideLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport);

    // Submits just the active-body wireframe, bright and on-top (no depth test), into
    // the current render pass. Used as the bloom glow source so the glow comes from the
    // full, unclipped line rather than the depth-tested (and self-clipped) scene copy.
    void SubmitActiveGlowSource(const FrameContext& frame, const EditorViewport& viewport);

private:
    // Every edge of the mesh, for the selected/preview-mesh wireframe overlay.
    void AppendWireframe(std::vector<EditorLineSegment>& segments,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color,
                         float widthPx) const;
    void AppendFace(std::vector<EditorLineSegment>& segments,
                    const FaceElement& face,
                    const Vec4& color,
                    float widthPx) const;
    void AppendEdge(std::vector<EditorLineSegment>& segments,
                    const EdgeElement& edge,
                    const Vec4& color,
                    float widthPx) const;
    // A screen-constant square at a vertex (the visible vertex handle).
    void AppendVertexSquare(std::vector<EditorLineSegment>& segments,
                            const EditorViewport& viewport,
                            Vec3d position,
                            const Vec4& color,
                            float widthPx) const;
    // Entities referenced by the current selection (as objects or via element refs):
    // selecting any element makes the whole mesh the active body.
    [[nodiscard]] std::vector<EntityId> GatherActiveBodies() const;
    void AppendHover(std::vector<EditorLineSegment>& segments, const EditorViewport& viewport) const;
    void AppendManipulators(std::vector<EditorLineSegment>& segments,
                            const EditorViewport& viewport) const;

    EditorScene& Scene;
    SelectionService& Selection;
    MeshEditService& MeshEdit;
    const EditorOverlayState& Overlay;
    ManipulatorSession& Session;
    EditorWideLinePipeline& Lines;
};
