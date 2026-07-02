#include "SceneRenderQueueBuilder.h"

#include "document/BrushCookInput.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <assets/cook/BrushClustering.h>   // CookBrushGeometry
#include <assets/cook/BrushGeometryCook.h> // CollectMaterialOrder, BakeBrushFacesToStaticMesh
#include <core/assets/AssetSystem.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <render/MaterialSetCache.h>
#include <render/PointLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <world/registry/Registry.h>

#include <cstddef>
#include <utility>

namespace
{
    constexpr uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;

    void HashBytes(uint64_t& h, const void* data, std::size_t n)
    {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i)
        {
            h ^= p[i];
            h *= kFnvPrime;
        }
    }

    // Content hash of the collected brushes: the bake is skipped (no GPU upload)
    // when this is unchanged. Covers each face's material path and the input
    // vertices (position/normal/uv); tangents are bake output, not input.
    uint64_t HashBrushes(const std::vector<CookBrushGeometry>& brushes)
    {
        uint64_t h = kFnvOffset;
        for (const CookBrushGeometry& brush : brushes)
        {
            for (const CookFace& face : brush.Faces)
            {
                HashBytes(h, face.Material.Path.data(), face.Material.Path.size());
                for (const StaticMeshVertex& v : face.Triangles)
                {
                    HashBytes(h, &v.Position, sizeof(v.Position));
                    HashBytes(h, &v.Normal, sizeof(v.Normal));
                    HashBytes(h, &v.Uv0, sizeof(v.Uv0));
                }
            }
            HashBytes(h, "|", 1); // brush boundary, so regrouping faces changes the hash
        }
        return h;
    }

    Aabb3d TransformBounds(const Aabb3d& local, const Mat4& world)
    {
        Aabb3d result = Aabb3d::Empty();
        for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
        for (int z = 0; z < 2; ++z)
        {
            Vec3d p(x == 0 ? local.Min.X : local.Max.X,
                    y == 0 ? local.Min.Y : local.Max.Y,
                    z == 0 ? local.Min.Z : local.Max.Z);
            result.ExpandToInclude(world.TransformPoint(p));
        }
        return result;
    }
}

SceneRenderQueueBuilder::SceneRenderQueueBuilder(const EditorDocument& document,
                                                 AssetSystem& assets,
                                                 StaticMeshCache& meshes,
                                                 MaterialSetCache& materialSets,
                                                 LoggingProvider& logging)
    : Document(document)
    , Assets(assets)
    , Meshes(meshes)
    , MaterialSets(materialSets)
    , Log(logging.GetLogger<SceneRenderQueueBuilder>())
{
}

SceneRenderQueueBuilder::~SceneRenderQueueBuilder()
{
    ReleaseBrushMeshes();
}

void SceneRenderQueueBuilder::Build()
{
    RebuildBrushMeshes();
    EmitBrushQueue();
    BuildMeshQueue();
    BuildLights();
}

void SceneRenderQueueBuilder::RebuildBrushMeshes()
{
    // Same kernel the cook and PIE use, so the preview is the cooked geometry.
    std::vector<CookBrushGeometry> brushes =
        CollectCookBrushes(Document.GetScene(), Document.GetDefaultMaterial());

    const uint64_t hash = HashBrushes(brushes);
    if (HasBaked && hash == BrushHash)
        return; // brushes unchanged since the last bake — nothing to re-upload

    std::vector<CachedBrushMesh> built;
    std::vector<MaterialHandle> acquired;
    built.reserve(brushes.size());
    for (const CookBrushGeometry& brush : brushes)
    {
        const std::vector<AssetRef> order = CollectMaterialOrder(brush.Faces);

        MeshGeometry geometry;
        std::string error;
        if (!BakeBrushFacesToStaticMesh(brush.Faces, order, geometry, &error))
        {
            Log.Warn("brush bake failed: {}", error);
            continue;
        }

        const StaticMeshHandle mesh = Meshes.Create(geometry);
        if (!mesh.IsValid())
            continue;

        CachedBrushMesh entry;
        entry.Mesh = mesh;
        entry.SlotMaterials.reserve(order.size());
        for (const AssetRef& ref : order)
        {
            const MaterialHandle material = Assets.LoadMaterial(ref.Path);
            entry.SlotMaterials.push_back(material);
            if (material.IsValid())
                acquired.push_back(material);
        }
        built.push_back(std::move(entry));
    }

    // Acquired the new refs already, so releasing the old set here cannot free a
    // material the new build still needs (no free/reload churn for shared ones).
    ReleaseBrushMeshes();
    BrushMeshes = std::move(built);
    BrushMaterials = std::move(acquired);
    BrushHash = hash;
    HasBaked = true;
}

void SceneRenderQueueBuilder::EmitBrushQueue()
{
    Brushes.Reset();
    for (const CachedBrushMesh& entry : BrushMeshes)
    {
        const GpuStaticMesh* mesh = Meshes.Get(entry.Mesh);
        if (mesh == nullptr)
            continue;

        for (uint32_t section = 0; section < static_cast<uint32_t>(mesh->Sections.size()); ++section)
        {
            const uint32_t slot = mesh->Sections[section].MaterialSlot;
            const MaterialHandle material =
                slot < entry.SlotMaterials.size() ? entry.SlotMaterials[slot] : MaterialHandle{};
            if (!material.IsValid())
                continue;

            RenderQueueItem item{};
            item.Mesh = entry.Mesh;
            item.Material = material;
            item.SectionIndex = section;
            // Brush geometry is baked in world space (BrushTessellate), so it sits
            // at identity; its section bounds are already world-space.
            item.WorldMatrix = Mat4::Identity();
            item.WorldBounds = mesh->Sections[section].LocalBounds;
            Brushes.AddOpaque(item);
        }
    }
    Brushes.SortOpaque();
}

void SceneRenderQueueBuilder::BuildMeshQueue()
{
    PlacedMeshes.Reset();

    const EditorScene& scene = Document.GetScene();
    const World& world = scene.GetRegistry().Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        const StaticMeshComponent* renderer = world.TryGet<StaticMeshComponent>(entity);
        if (renderer == nullptr || !renderer->Visible)
            continue;

        const GpuStaticMesh* mesh = Meshes.Get(renderer->Mesh);
        const std::vector<MaterialHandle>* sectionMaterials = MaterialSets.Get(renderer->Materials);
        if (mesh == nullptr || sectionMaterials == nullptr || sectionMaterials->empty())
            continue;
        const Transform3f* transform = scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        const Mat4 worldMatrix = transform->ToMat4();
        const Aabb3d worldBounds = TransformBounds(mesh->LocalBounds, worldMatrix);

        for (uint32_t section = 0; section < static_cast<uint32_t>(mesh->Sections.size()); ++section)
        {
            if ((renderer->SectionMask & (1u << section)) == 0)
                continue;

            // Map section to material via MaterialSlot, last-member fallback for an
            // under-bound set — identical to RenderExtractionSystem so a placed mesh
            // reads the same in the editor and the game.
            const uint32_t slot = mesh->Sections[section].MaterialSlot;
            const MaterialHandle material = slot < sectionMaterials->size()
                ? (*sectionMaterials)[slot]
                : sectionMaterials->back();
            if (!material.IsValid())
                continue;

            RenderQueueItem item{};
            item.Mesh = renderer->Mesh;
            item.Material = material;
            item.SectionIndex = section;
            item.WorldMatrix = worldMatrix;
            item.WorldBounds = worldBounds;
            PlacedMeshes.AddOpaque(item);
        }
    }
    PlacedMeshes.SortOpaque();
}

void SceneRenderQueueBuilder::BuildLights()
{
    // Reset() clears only the light count; the ambient tints are owned by the
    // caller (EditorRenderFeature sets them from render.ambient.* cvars), so we
    // leave them untouched here.
    SceneLights.Reset();

    const EditorScene& scene = Document.GetScene();
    const World& world = scene.GetRegistry().Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        const PointLightComponent* light = world.TryGet<PointLightComponent>(entity);
        if (light == nullptr || !light->Enabled)
            continue;
        const Transform3f* transform = scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        SceneLights.AddPoint(transform->Position, *light);
    }
}

void SceneRenderQueueBuilder::ReleaseBrushMeshes()
{
    for (const CachedBrushMesh& entry : BrushMeshes)
        Meshes.Destroy(entry.Mesh);
    BrushMeshes.clear();

    for (const MaterialHandle material : BrushMaterials)
        Assets.ReleaseMaterial(material);
    BrushMaterials.clear();
}
