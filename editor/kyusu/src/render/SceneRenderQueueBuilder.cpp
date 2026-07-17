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
#include <render/ShadowCasterExtractionSystem.h>
#include <render/SpotLightComponent.h>
#include <render/RenderEntityKey.h>
#include <render/StaticMeshComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <world/registry/Registry.h>

#include <algorithm>
#include <cmath>
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

SceneRenderQueueBuilder::SceneRenderQueueBuilder(AssetSystem& assets,
                                                 StaticMeshCache& meshes,
                                                 MaterialCache& materials,
                                                 MaterialSetCache& materialSets,
                                                 LoggingProvider& logging)
    : Assets(assets)
    , Meshes(meshes)
    , Materials(materials)
    , MaterialSets(materialSets)
    , Log(logging.GetLogger<SceneRenderQueueBuilder>())
{
}

SceneRenderQueueBuilder::~SceneRenderQueueBuilder()
{
    ReleaseBrushMeshes();
}

void SceneRenderQueueBuilder::Build(const EditorDocument& document)
{
    RebuildBrushMeshes(document);
    EmitBrushQueue();
    BuildMeshQueue(document);
    BuildLights(document);
    BuildShadowCasters(document);
}

void SceneRenderQueueBuilder::RebuildBrushMeshes(const EditorDocument& document)
{
    // Same kernel the cook and PIE use, so the preview is the cooked geometry.
    std::vector<CookBrushGeometry> brushes =
        CollectCookBrushes(document.GetScene(), document.GetDefaultMaterial());

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

void SceneRenderQueueBuilder::BuildMeshQueue(const EditorDocument& document)
{
    PlacedMeshes.Reset();

    const EditorScene& scene = document.GetScene();
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

void SceneRenderQueueBuilder::BuildLights(const EditorDocument& document)
{
    // Reset() clears only the packed counts; the ambient tints and shadow
    // tunables are owned by the caller (EditorRenderFeature stamps them from
    // render.* cvars before Build), so we leave them untouched here. The
    // shadow-view gather below reads ShadowSoftness, which is why the stamp
    // has to happen first.
    SceneLights.Reset();
    ShadowCandidates.clear();
    PointShadowCandidates.clear();

    const EditorScene& scene = document.GetScene();
    const Registry& registry = scene.GetRegistry();
    const World& world = registry.Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        const Transform3f* transform = scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        if (const PointLightComponent* point = world.TryGet<PointLightComponent>(entity);
            point != nullptr && point->Enabled
            && std::isfinite(point->Intensity) && std::isfinite(point->Range)
            && point->Intensity > 0.0f && point->Range > 0.0f)
        {
            const std::uint32_t lightIndex = SceneLights.Add(
                MakePointGpuLight(transform->Position, *point));
            if (lightIndex != UINT32_MAX && point->CastShadows)
            {
                PointShadowCandidates.push_back(PointShadowCandidate{
                    .Key = MakeRenderEntityKey(registry, entity),
                    .LightIndex = lightIndex,
                    .Position = transform->Position,
                    .Range = point->Range,
                    .Intensity = point->Intensity,
                    .View = MakePointShadowView(
                        transform->Position, *point, SceneLights.ShadowSoftness),
                    .Bounds = Sphere(transform->Position, point->Range),
                    .Policy = point->ShadowUpdate,
                });
            }
        }

        const SpotLightComponent* spot = world.TryGet<SpotLightComponent>(entity);
        if (spot == nullptr || !spot->Enabled
            || !std::isfinite(spot->Intensity) || !std::isfinite(spot->Range)
            || spot->Intensity <= 0.0f || spot->Range <= 0.0f)
        {
            continue;
        }

        const Vec<3> direction = transform->Forward();
        const std::uint32_t lightIndex = SceneLights.Add(
            MakeSpotGpuLight(transform->Position, direction, *spot));
        if (lightIndex != UINT32_MAX && spot->CastShadows)
        {
            ShadowCandidates.push_back(SpotShadowCandidate{
                .Key = MakeRenderEntityKey(registry, entity),
                .LightIndex = lightIndex,
                .Position = transform->Position,
                .Range = spot->Range,
                .Intensity = spot->Intensity,
                .View = MakeSpotShadowView(*transform, *spot, SceneLights.ShadowSoftness),
                .Bounds = MakeSpotBoundingSphere(
                    transform->Position, direction, spot->Range,
                    spot->OuterAngleDegrees),
                .TileSize = static_cast<std::uint32_t>(spot->ShadowResolution),
                .Policy = spot->ShadowUpdate,
            });
        }
    }
}

std::span<const SpotShadowRequest> SceneRenderQueueBuilder::BuildShadowRequests(
    const Vec<3>& viewOrigin)
{
    ShadowRequests.clear();
    ShadowRequests.reserve(ShadowCandidates.size());
    for (const SpotShadowCandidate& candidate : ShadowCandidates)
    {
        ShadowRequests.push_back(SpotShadowRequest{
            .Key = candidate.Key,
            .LightIndex = candidate.LightIndex,
            .Score = LightImportanceScore(
                candidate.Position, candidate.Range, candidate.Intensity, viewOrigin),
            .TileSize = candidate.TileSize,
            .Policy = candidate.Policy,
            .StateHash = HashSpotShadowState(candidate.View, candidate.TileSize),
            .ViewProjection = candidate.View.ViewProjection,
            .SamplingParams = candidate.View.SamplingParams,
            .Bounds = candidate.Bounds,
        });
    }
    std::sort(ShadowRequests.begin(), ShadowRequests.end(),
        [](const SpotShadowRequest& a, const SpotShadowRequest& b)
        {
            if (a.Score != b.Score)
                return a.Score > b.Score;
            return a.Key < b.Key;
        });
    return ShadowRequests;
}

std::span<const PointShadowRequest> SceneRenderQueueBuilder::BuildPointShadowRequests(
    const Vec<3>& viewOrigin)
{
    PointShadowRequests.clear();
    PointShadowRequests.reserve(PointShadowCandidates.size());
    for (const PointShadowCandidate& candidate : PointShadowCandidates)
    {
        PointShadowRequests.push_back(PointShadowRequest{
            .Key = candidate.Key,
            .LightIndex = candidate.LightIndex,
            .Score = LightImportanceScore(
                candidate.Position, candidate.Range, candidate.Intensity, viewOrigin),
            .Policy = candidate.Policy,
            .StateHash = HashPointShadowState(candidate.View),
            .View = candidate.View,
            .Bounds = candidate.Bounds,
        });
    }
    std::sort(PointShadowRequests.begin(), PointShadowRequests.end(),
        [](const PointShadowRequest& a, const PointShadowRequest& b)
        {
            if (a.Score != b.Score)
                return a.Score > b.Score;
            return a.Key < b.Key;
        });
    return PointShadowRequests;
}

void SceneRenderQueueBuilder::BuildShadowCasters(const EditorDocument& document)
{
    SceneCasters.Reset();

    const EditorScene& scene = document.GetScene();
    const Registry& registry = scene.GetRegistry();

    // Brush geometry is baked in world space at identity, so its mesh bounds
    // are already world bounds. Every section is offered; the engine caster
    // policy drops sections whose material opts out. Cooked brushes have no
    // entity, so their diff records key on the bake ordinal in a high-bit
    // index namespace real entities cannot reach; a rebake recreates every
    // brush mesh handle, so any brush edit reads as changed records over the
    // affected bounds.
    std::uint32_t brushOrdinal = 0;
    for (const CachedBrushMesh& entry : BrushMeshes)
    {
        const GpuStaticMesh* mesh = Meshes.Get(entry.Mesh);
        if (mesh == nullptr)
            continue;
        const ShadowCasterGatherResult gathered = AppendShadowCasterSections(
            entry.Mesh, *mesh, entry.SlotMaterials, Materials,
            ~0u, Mat4::Identity(), mesh->LocalBounds, SceneCasters);
        const std::uint32_t ordinal = brushOrdinal++;
        if (gathered.EffectiveSectionMask == 0)
            continue;

        RenderEntityKey key = MakeRenderEntityKey(
            registry, EntityId{ .Index = 0x80000000u | ordinal, .Generation = 0 });
        SceneCasters.Records.push_back(ShadowCasterRecord{
            .Key = key,
            .State = ShadowCasterState{
                .WorldBounds = QuantizeShadowCasterBounds(gathered.WorldBounds),
                .Mesh = entry.Mesh,
                .Materials = MaterialSetHandle{},
                .EffectiveShadowSectionMask = gathered.EffectiveSectionMask,
                .ShadowMaterialStateHash = gathered.MaterialStateHash,
            },
        });
    }

    const World& world = registry.Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityVisible(entity))
            continue;
        const StaticMeshComponent* renderer = world.TryGet<StaticMeshComponent>(entity);
        if (renderer == nullptr)
            continue;
        const GpuStaticMesh* mesh = Meshes.Get(renderer->Mesh);
        const std::vector<MaterialHandle>* sectionMaterials =
            MaterialSets.Get(renderer->Materials);
        const Transform3f* transform = scene.TryGetTransform(entity);
        if (mesh == nullptr || sectionMaterials == nullptr || transform == nullptr)
            continue;

        const ShadowCasterGatherResult gathered = AppendShadowCasters(
            *renderer, *mesh, *sectionMaterials, Materials,
            transform->ToMat4(), SceneCasters);
        if (gathered.EffectiveSectionMask == 0)
            continue;

        SceneCasters.Records.push_back(ShadowCasterRecord{
            .Key = MakeRenderEntityKey(registry, entity),
            .State = ShadowCasterState{
                .WorldBounds = QuantizeShadowCasterBounds(gathered.WorldBounds),
                .Mesh = renderer->Mesh,
                .Materials = renderer->Materials,
                .EffectiveShadowSectionMask = gathered.EffectiveSectionMask,
                .ShadowMaterialStateHash = gathered.MaterialStateHash,
            },
        });
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
