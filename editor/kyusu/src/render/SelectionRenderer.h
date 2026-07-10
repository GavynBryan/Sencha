#pragma once

#include "EditorFillPipeline.h"
#include "EditorWideLinePipeline.h"

#include "editmodes/IManipulator.h"
#include "document/EditorScene.h"
#include "brush/BrushMesh.h"
#include "meshedit/MeshElements.h"
#include "selection/SelectionService.h"
#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

#include <cstdint>
#include <unordered_map>
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
    SelectionRenderer(SelectionService& selection, MeshEditService& meshEdit,
                      const EditorOverlayState& overlay,
                      EditorWideLinePipeline& lines, EditorFillPipeline& fill);

    // Drops the per-frame edge caches. Call once per frame before the viewport
    // draws: the caches are keyed only by entity, so they must not survive a
    // frame boundary where meshes or transforms may have changed.
    void BeginFrame();

    // Scene and session are per-call: both are rebuilt when the workspace swaps
    // the edited document, so the renderer holds neither.
    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const EditorScene& scene, const ManipulatorSession& session);

    // Submits just the active-body wireframe, bright and on-top (no depth test), into
    // the current render pass. Used as the bloom glow source so the glow comes from the
    // full, unclipped line rather than the depth-tested (and self-clipped) scene copy.
    void SubmitActiveGlowSource(const FrameContext& frame, const EditorViewport& viewport,
                                const EditorScene& scene);

private:
    // Every edge of the mesh, for the selected/preview-mesh wireframe overlay.
    void AppendWireframe(std::vector<EditorLineSegment>& segments,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         EntityId entity,
                         const Vec4& color,
                         float widthPx);
    void AppendFace(std::vector<EditorLineSegment>& segments,
                    const FaceElement& face,
                    const Vec4& color,
                    float widthPx) const;
    void AppendFaceFill(std::vector<EditorLineVertex>& triangles,
                        const BrushMesh& mesh,
                        const Transform3f& transform,
                        std::uint32_t faceIndex,
                        const Vec4& color) const;
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
    [[nodiscard]] std::vector<EntityId> GatherActiveBodies(const EditorScene& scene) const;
    void AppendHover(std::vector<EditorLineSegment>& segments, const EditorViewport& viewport,
                     const EditorScene& scene);
    void AppendManipulators(std::vector<EditorLineSegment>& segments,
                            const EditorViewport& viewport,
                            const ManipulatorSession& session) const;

    // The entity's world-space edge list, built once per frame and shared by
    // every viewport, wireframe pass, and selected-edge ref. Rebuilding it per
    // ref (MeshElements::TryGetEdge re-enumerates the whole mesh) made large
    // edge selections quadratic per frame.
    [[nodiscard]] const std::vector<EdgeElement>& EdgesFor(EntityId entity,
                                                           const BrushMesh& mesh,
                                                           const Transform3f& transform);

    SelectionService& Selection;
    MeshEditService& MeshEdit;
    const EditorOverlayState& Overlay;
    EditorWideLinePipeline& Lines;
    EditorFillPipeline& Fill;
    std::unordered_map<std::uint64_t, std::vector<EdgeElement>> EdgeCache; // key: entity index|generation
};
