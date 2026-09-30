#pragma once

#include "EditorFillPipeline.h"
#include "BrushDrawSet.h"
#include "BrushFaceHighlight.h"
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
#include <utility>
#include <vector>

class ManipulatorSession;
class MeshEditService;
struct EditorOverlayState;

// Draws selection highlights (object/face/edge/vertex), the hovered element glow,
// and the active manipulators via the wide-line pipeline. Gathers segments only;
// the active body draws bolder than preview/hover so it reads as primary.
class EditorInstancedLinePipeline;
class EditorInstancedFillPipeline;

class SelectionRenderer
{
public:
    SelectionRenderer(SelectionService& selection, MeshEditService& meshEdit,
                      const EditorOverlayState& overlay,
                      EditorWideLinePipeline& lines, EditorFillPipeline& fill);

    // Instanced highlighting of generated pieces over the builder's retained
    // draws: a selected body's source piece keeps the wide anti-aliased stroke
    // and its copies draw as instanced thin lines; a selected face is one
    // geometry per distinct mesh instanced over that mesh's placements. Without
    // it (no asset environment) every piece draws as wide segments.
    void SetInstancing(const BrushDrawSet* draws, EditorInstancedLinePipeline* lines,
                       EditorInstancedFillPipeline* fill);

    // Advances the frame clock the retained bodies and face highlights are
    // swept by. Call once per frame before the viewport draws.
    void BeginFrame();

    // Scene and session are per-call: both are rebuilt when the workspace swaps
    // the edited document, so the renderer holds neither.
    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const CameraRenderData& camera,
                      const EditorScene& scene, const ManipulatorSession& session);

    // Submits just the active-body wireframe, bright and on-top (no depth test), into
    // the current render pass. Used as the bloom glow source so the glow comes from the
    // full, unclipped line rather than the depth-tested (and self-clipped) scene copy.
    void SubmitActiveGlowSource(const FrameContext& frame, const EditorViewport& viewport,
                                const CameraRenderData& camera,
                                const EditorScene& scene);

private:
    // Every edge of the mesh at `transform`, for the selected/preview-mesh
    // wireframe overlay. Edge topology comes from the per-mesh pair cache, so
    // a body of many placements enumerates each distinct mesh once.
    void AppendWireframe(std::vector<EditorLineSegment>& segments,
                         const BrushEvaluatedMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color,
                         float widthPx);
    struct InstancedBody
    {
        const BrushDrawEntity* Record = nullptr;
        Vec4 Color;
    };
    struct InstancedFace
    {
        const BrushDrawEntity* Record = nullptr;
        const BrushFaceHighlight* Geometry = nullptr;
    };

    // The entity's body as wide segments: every piece without instancing, the
    // source piece alone with it (the copies are queued on `copies`).
    void AppendBodyWireframe(std::vector<EditorLineSegment>& segments,
                             const EditorScene& scene, EntityId entity,
                             const Vec4& color, float widthPx,
                             std::vector<InstancedBody>& copies);
    void SubmitInstancedCopies(const FrameContext& frame, const EditorViewport& viewport,
                               const CameraRenderData& camera,
                               std::span<const InstancedBody> copies, bool onTop);
    // A selected source face: wide outline on the source piece, instanced fill
    // and thin outline on every placement of every mesh carrying the face.
    void AppendFaceHighlight(std::vector<EditorLineSegment>& outline,
                             std::vector<EditorLineVertex>& fill,
                             const EditorScene& scene, EntityId entity,
                             std::uint32_t sourceFace, const Vec4& outlineColor, float widthPx,
                             std::vector<InstancedFace>& instanced);
    void SubmitInstancedFaces(const FrameContext& frame, const EditorViewport& viewport,
                              const CameraRenderData& camera,
                              std::span<const InstancedFace> faces, bool onTop);
    [[nodiscard]] const BrushFaceHighlight& FaceHighlightFor(EntityId entity, std::uint32_t sourceFace,
                                                             const BrushEvaluated& evaluated,
                                                             std::uint64_t topologyRevision);

    // The retained wide segments of the entity's body: every piece, or the
    // source piece alone when the copies are instanced.
    void AppendBodyWireframeAllPieces(std::vector<EditorLineSegment>& segments,
                                      const EditorScene& scene,
                                      EntityId entity,
                                      const Vec4& color,
                                      float widthPx,
                                      bool sourceOnly);
    // The plane of every enabled Mirror on the brush, as a rectangle sized to
    // the source, so a designer sees what the copy reflects across.
    void AppendMirrorPlanes(std::vector<EditorLineSegment>& segments,
                            const EditorScene& scene,
                            EntityId entity) const;
    // Every enabled Array on the brush: the repeated set's extent along the
    // step, one tick per copy, read from the evaluation's stage resolutions.
    void AppendArrayExtent(std::vector<EditorLineSegment>& segments,
                           const EditorScene& scene,
                           EntityId entity) const;
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
                     const EditorScene& scene, std::vector<InstancedBody>& copies);
    void AppendManipulators(std::vector<EditorLineSegment>& segments,
                            const EditorViewport& viewport,
                            const ManipulatorSession& session) const;

    SelectionService& Selection;
    MeshEditService& MeshEdit;
    const EditorOverlayState& Overlay;
    EditorWideLinePipeline& Lines;
    EditorFillPipeline& Fill;
    // A body's wide-line segments, retained across frames while the entity's
    // placements, colour and width stay what they were built for, so a selected
    // array of hundreds of copies is one bulk append per pass instead of a
    // transform per edge per piece. Entries not touched in a frame are dropped
    // at the next BeginFrame, so the map holds only current bodies.
    struct RetainedBody
    {
        BrushPlacementKey Key;
        Vec4  Color;
        float WidthPx = 0.0f;
        bool  SourceOnly = false;
        std::vector<EditorLineSegment> Segments;
        std::uint64_t LastUsedFrame = 0;
    };
    struct RetainedFace
    {
        BrushFaceHighlight Geometry;
        std::uint64_t LastUsedFrame = 0;
    };
    std::unordered_map<std::uint64_t, RetainedBody> Bodies;       // key: entity index|generation
    std::unordered_map<std::uint64_t, RetainedFace> FaceHighlights; // key: entity index|generation ^ face
    std::uint64_t FrameCounter = 0;
    std::vector<EditorLineVertex> InstanceScratch; // one mesh's coloured geometry, reused

    const BrushDrawSet*          Draws = nullptr;
    EditorInstancedLinePipeline* InstancedLines = nullptr;
    EditorInstancedFillPipeline* InstancedFill = nullptr;
};
