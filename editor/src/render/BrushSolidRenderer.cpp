#include "BrushSolidRenderer.h"

#include "../level/SceneBrushWalk.h"
#include "../brush/BrushTessellation.h"
#include "../brush/FaceMaterial.h"

#include <cstdint>
#include <span>

namespace
{
    // Stable, light pastel tint per material so distinct materials read
    // differently without overpowering the checker. Empty (level-default) faces
    // stay neutral white.
    Vec4 TintForMaterial(const AssetRef& material)
    {
        if (material.Path.empty())
            return Vec4(1.0f, 1.0f, 1.0f, 1.0f);

        std::uint32_t hash = 2166136261u; // FNV-1a
        for (char c : material.Path)
        {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= 16777619u;
        }
        // Map the hash to a pale color: keep each channel high so it tints rather
        // than darkens.
        const float r = 0.65f + 0.35f * ((hash & 0xFF) / 255.0f);
        const float g = 0.65f + 0.35f * (((hash >> 8) & 0xFF) / 255.0f);
        const float b = 0.65f + 0.35f * (((hash >> 16) & 0xFF) / 255.0f);
        return Vec4(r, g, b, 1.0f);
    }
}

BrushSolidRenderer::BrushSolidRenderer(LevelScene& scene, EditorSolidPipeline& solid)
    : Scene(scene)
    , Solid(solid)
{
}

void BrushSolidRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    std::vector<EditorSolidVertex> vertices;
    vertices.reserve(Scene.GetEntityCount() * 36);
    ForEachVisibleBrush(Scene, /*skipLocked*/ false,
        [&](EntityId, const BrushMesh& mesh, const Transform3f& transform)
        { AppendBrushMesh(vertices, mesh, transform); });

    Solid.Submit(frame, viewport, vertices);
}

void BrushSolidRenderer::AppendBrushMesh(std::vector<EditorSolidVertex>& vertices,
                                         const BrushMesh& mesh,
                                         const Transform3f& transform) const
{
    // The kernel produces the triangles (positions/normals/UVs); the renderer only
    // adds the per-material tint and packs them into GPU vertices.
    BrushTessellate(mesh, transform,
        [&](const FaceMaterial& material, std::span<const BrushTriVertex> triangles) {
            const Vec4 tint = TintForMaterial(material.Material);
            for (const BrushTriVertex& v : triangles)
                vertices.push_back(EditorSolidVertex{
                    .Position = v.Position, .Normal = v.Normal, .Uv = v.Uv, .Tint = tint });
        });
}
