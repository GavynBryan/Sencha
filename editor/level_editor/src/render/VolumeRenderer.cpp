#include "VolumeRenderer.h"

#include "BrushDrawSet.h"
#include "EditorInstancedFillPipeline.h"
#include "EditorInstancedLinePipeline.h"
#include "EditorTheme.h"

#include <span>

VolumeRenderer::VolumeRenderer(EditorInstancedFillPipeline& fill, EditorInstancedLinePipeline& lines)
    : Fill(fill)
    , Lines(lines)
{
}

void VolumeRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                  const CameraRenderData& camera, const BrushDrawSet& draws)
{
    for (const BrushDrawEntity& entity : draws.Entities())
    {
        for (const BrushMeshRun& run : entity.Runs)
        {
            const BrushDrawMesh& mesh = entity.Meshes[run.MeshIndex];
            if (mesh.Role != BrushRole::Volume)
                continue;
            const std::span<const ColoredVertexInstance> placements =
                AsLineInstances(std::span(entity.InstanceRows).subspan(run.FirstPlacement, run.PlacementCount));
            Scratch.clear();
            for (const Vec3d& position : mesh.VolumeFill)
                Scratch.push_back(ColoredVertex{ .Position = position, .Color = EditorTheme::VolumeFill });
            if (!Scratch.empty())
                Fill.Submit(frame, viewport, camera, Scratch, placements);
            Scratch.clear();
            for (const BrushEdgeVertex& edge : mesh.Edges)
                Scratch.push_back(ColoredVertex{ .Position = edge.Position, .Color = EditorTheme::VolumeEdge });
            if (!Scratch.empty())
                Lines.Submit(frame, viewport, camera, Scratch, placements);
        }
    }
}
