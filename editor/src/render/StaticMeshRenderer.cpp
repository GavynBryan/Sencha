#include "StaticMeshRenderer.h"

#include "../document/EditorScene.h"

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <render/StaticMeshComponent.h>
#include <world/registry/Registry.h>

#include <cstdint>
#include <utility>
#include <vector>

StaticMeshRenderer::StaticMeshRenderer(EditorScene& scene,
                                       EditorSolidPipeline& solid,
                                       LoggingProvider& logging,
                                       AssetSystem* assets,
                                       const AssetRegistry* catalog)
    : Scene(scene)
    , Solid(solid)
    , Loader(logging)
    , Assets(assets)
    , Catalog(catalog)
{
}

const MeshGeometry* StaticMeshRenderer::GeometryFor(const std::string& assetPath)
{
    if (const auto it = Cache.find(assetPath); it != Cache.end())
        return it->second.Vertices.empty() ? nullptr : &it->second;

    MeshGeometry geometry; // empty on any failure, cached so we don't retry each frame
    if (Catalog != nullptr)
        if (const AssetRecord* record = Catalog->FindByPath(assetPath);
            record != nullptr && !record->FilePath.empty())
            (void)Loader.LoadFromFile(record->FilePath, geometry);

    const MeshGeometry& stored = Cache.emplace(assetPath, std::move(geometry)).first->second;
    return stored.Vertices.empty() ? nullptr : &stored;
}

void StaticMeshRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    if (Assets == nullptr)
        return;

    // const access: reading components for draw must not churn change tracking.
    const World& world = std::as_const(Scene).GetRegistry().Components;

    std::vector<EditorSolidVertex> vertices;
    for (EntityId entity : Scene.GetAllEntities())
    {
        if (!Scene.IsEntityVisible(entity))
            continue;
        const StaticMeshComponent* mesh = world.TryGet<StaticMeshComponent>(entity);
        if (mesh == nullptr)
            continue;

        const std::string path(Assets->GetPathForStaticMesh(mesh->Mesh));
        if (path.empty())
            continue;
        const MeshGeometry* geometry = GeometryFor(path);
        if (geometry == nullptr)
            continue;
        const Transform3f* transform = Scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        // Flat neutral tint: enough to read placement and shape. Material color in
        // the editor is a later pass; the runtime renders the real material.
        const Vec4 tint(0.80f, 0.80f, 0.85f, 1.0f);
        for (const std::uint32_t index : geometry->Indices)
        {
            const StaticMeshVertex& v = geometry->Vertices[index];
            vertices.push_back(EditorSolidVertex{
                .Position = transform->TransformPoint(v.Position),
                .Normal = transform->Rotation.RotateVector(v.Normal),
                .Uv = v.Uv0,
                .Tint = tint,
            });
        }
    }

    Solid.Submit(frame, viewport, vertices);
}
