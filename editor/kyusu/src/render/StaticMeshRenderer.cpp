#include "StaticMeshRenderer.h"

#include "document/EditorScene.h"

#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <core/logging/LoggingProvider.h>
#include <render/StaticMeshComponent.h>
#include <world/registry/Registry.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

StaticMeshRenderer::StaticMeshRenderer(EditorSolidPipeline& solid,
                                       LoggingProvider& logging,
                                       AssetSystem* assets,
                                       const AssetRegistry* catalog)
    : Solid(solid)
    , Loader(logging)
    , Log(&logging.GetLogger<StaticMeshRenderer>())
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
        {
            (void)Loader.LoadFromFile(record->FilePath, geometry);
            PruneInvalidIndices(assetPath, geometry);
        }

    const MeshGeometry& stored = Cache.emplace(assetPath, std::move(geometry)).first->second;
    return stored.Vertices.empty() ? nullptr : &stored;
}

void StaticMeshRenderer::PruneInvalidIndices(const std::string& assetPath, MeshGeometry& geometry) const
{
    std::size_t write = 0;
    std::size_t dropped = 0;
    for (const std::uint32_t index : geometry.Indices)
    {
        if (index < geometry.Vertices.size())
        {
            geometry.Indices[write] = index;
            ++write;
        }
        else
        {
            ++dropped;
        }
    }
    geometry.Indices.resize(write);

    if (dropped > 0 && Log != nullptr)
    {
        Log->Warn("StaticMeshRenderer: dropped {} invalid indices from '{}' ({} vertices)",
                  dropped, assetPath, geometry.Vertices.size());
    }
}

void StaticMeshRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                                      const CameraRenderData& camera,
                                      const EditorScene& scene)
{
    if (Assets == nullptr)
        return;

    // const access: reading components for draw must not churn change tracking.
    const World& world = scene.GetRegistry().Components;

    std::vector<EditorSolidVertex> vertices;
    for (EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityEffectivelyVisible(entity))
            continue;
        const StaticMeshComponent* mesh = world.TryGet<StaticMeshComponent>(entity);
        if (mesh == nullptr)
            continue;

        const std::string path(
            Assets->GetPathForLease(AssetType::StaticMesh, mesh->Mesh.ToToken()));
        if (path.empty())
            continue;
        const MeshGeometry* geometry = GeometryFor(path);
        if (geometry == nullptr)
            continue;
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
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

    Solid.Submit(frame, viewport, camera, vertices);
}
