#include "BrushSolidRenderer.h"

#include "EditorTheme.h"
#include "document/EditorScene.h"
#include "document/SceneBrushWalk.h"
#include "brush/BrushTessellation.h"
#include "brush/FaceMaterial.h"

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

BrushSolidRenderer::BrushSolidRenderer(EditorSolidPipeline& solid)
    : Solid(solid)
{
}

void BrushSolidRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                      const EditorScene& scene)
{
    DrawViewportTinted(frame, viewport, scene, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
}

void BrushSolidRenderer::DrawViewportTinted(const FrameContext& frame, const EditorViewport& viewport,
                                            const EditorScene& scene, const Vec4& tint)
{
    std::vector<EditorSolidVertex> vertices;
    vertices.reserve(scene.GetEntityCount() * 36);
    ForEachVisibleBrush(scene, /*skipLocked*/ false,
        [&](EntityId id, const BrushMesh& mesh, const Transform3f& transform)
        {
            // Portal markers replace the per-material tint with the theme fill,
            // still modulated by the caller's tint (the context-zone dim).
            if (scene.IsPortal(id))
            {
                const Vec4 fill(EditorTheme::PortalFill.X * tint.X,
                                EditorTheme::PortalFill.Y * tint.Y,
                                EditorTheme::PortalFill.Z * tint.Z,
                                EditorTheme::PortalFill.W * tint.W);
                AppendBrushMeshFlat(vertices, mesh, transform, fill);
                return;
            }
            AppendBrushMesh(vertices, mesh, transform, tint);
        });

    Solid.Submit(frame, viewport, vertices);
}

void BrushSolidRenderer::AppendBrushMeshFlat(std::vector<EditorSolidVertex>& vertices,
                                             const BrushMesh& mesh,
                                             const Transform3f& transform,
                                             const Vec4& color) const
{
    BrushTessellate(mesh, transform,
        [&](const FaceMaterial&, std::span<const BrushTriVertex> triangles) {
            for (const BrushTriVertex& v : triangles)
                vertices.push_back(EditorSolidVertex{
                    .Position = v.Position, .Normal = v.Normal, .Uv = v.Uv, .Tint = color });
        });
}

void BrushSolidRenderer::AppendBrushMesh(std::vector<EditorSolidVertex>& vertices,
                                         const BrushMesh& mesh,
                                         const Transform3f& transform,
                                         const Vec4& tint) const
{
    // The kernel produces the triangles (positions/normals/UVs); the renderer only
    // adds the per-material tint and packs them into GPU vertices.
    BrushTessellate(mesh, transform,
        [&](const FaceMaterial& material, std::span<const BrushTriVertex> triangles) {
            const Vec4 base = TintForMaterial(material.Material);
            const Vec4 modulated(base.X * tint.X, base.Y * tint.Y, base.Z * tint.Z,
                                 base.W * tint.W);
            for (const BrushTriVertex& v : triangles)
                vertices.push_back(EditorSolidVertex{
                    .Position = v.Position, .Normal = v.Normal, .Uv = v.Uv, .Tint = modulated });
        });
}
