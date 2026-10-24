#include "VolumeRenderer.h"

#include "BrushDrawSet.h"
#include "EditorInstancedFillPipeline.h"
#include "EditorInstancedLinePipeline.h"
#include "EditorTheme.h"
#include "document/DocumentSerialization.h"

#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <optional>
#include <span>

namespace
{
struct VolumeTint
{
    const IComponentSerializer* Serializer = nullptr;
    Vec4 Fill;
    Vec4 Edge;
};

Vec4 ColourOf(const std::array<float, 4>& rgba)
{
    return Vec4(rgba[0], rgba[1], rgba[2], rgba[3]);
}

void CollectVolumeTints(std::vector<VolumeTint>& out)
{
    out.clear();
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        const std::optional<EditorVisual> visual = serializer->GetEditorVisual();
        if (visual && visual->VisualKind == EditorVisual::Kind::VolumeTint)
            out.push_back({ serializer.get(), ColourOf(visual->Fill), ColourOf(visual->Edge) });
    }
}
} // namespace

VolumeRenderer::VolumeRenderer(EditorInstancedFillPipeline& fill, EditorInstancedLinePipeline& lines)
    : Fill(fill)
    , Lines(lines)
{
}

void VolumeRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                  const CameraRenderData& camera, const BrushDrawSet& draws,
                                  const Registry* registry)
{
    std::vector<VolumeTint> tints;
    if (registry != nullptr)
        CollectVolumeTints(tints);
    for (const BrushDrawEntity& entity : draws.Entities())
    {
        Vec4 fill = EditorTheme::VolumeFill;
        Vec4 outline = EditorTheme::VolumeEdge;
        for (const VolumeTint& tint : tints)
            if (entity.Key.Role == BrushRole::Volume && tint.Serializer->HasComponent(entity.Key.Entity, *registry))
            {
                fill = tint.Fill;
                outline = tint.Edge;
                break;
            }
        for (const BrushMeshRun& run : entity.Runs)
        {
            const BrushDrawMesh& mesh = entity.Meshes[run.MeshIndex];
            if (mesh.Role != BrushRole::Volume)
                continue;
            const std::span<const ColoredVertexInstance> placements =
                AsLineInstances(std::span(entity.InstanceRows).subspan(run.FirstPlacement, run.PlacementCount));
            Scratch.clear();
            for (const Vec3d& position : mesh.VolumeFill)
                Scratch.push_back(ColoredVertex{ .Position = position, .Color = fill });
            if (!Scratch.empty())
                Fill.Submit(frame, viewport, camera, Scratch, placements);
            Scratch.clear();
            for (const BrushEdgeVertex& edge : mesh.Edges)
                Scratch.push_back(ColoredVertex{ .Position = edge.Position, .Color = outline });
            if (!Scratch.empty())
                Lines.Submit(frame, viewport, camera, Scratch, placements);
        }
    }
}
