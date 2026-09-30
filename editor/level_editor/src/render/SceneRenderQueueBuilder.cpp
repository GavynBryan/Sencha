#include "SceneRenderQueueBuilder.h"
#include "EditorRenderEntityKey.h"

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "render/EditorLightGather.h"

#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <assets/runtime/AssetSystem.h>
#include <core/hash/Fnv1a.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <math/geometry/3d/AabbTransform.h>
#include <assets/texture/TextureCache.h>
#include <render/MaterialSetCache.h>
#include <render/MeshDrawInstance.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <render/extract/RenderExtractionSystem.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/extract/ShadowCasterExtractionSystem.h>
#include <render/SpotLightComponent.h>
#include <render/RenderEntityKey.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <render/static_mesh/StaticMeshCache.h>
#include <world/registry/Registry.h>
#include <world/registry/SceneRegistryInitialization.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include "document/DocumentSerialization.h"

SceneRenderQueueBuilder::SceneRenderQueueBuilder(AssetSystem& assets,
                                                 BrushBakeCache& bakes,
                                                 StaticMeshCache& meshes,
                                                 MaterialCache& materials,
                                                 MaterialSetCache& materialSets,
                                                 LoggingProvider& logging,
                                                 TextureCache* textures,
                                                 SkinnedMeshCache* skinnedMeshes)
    : Assets(assets)
    , Bakes(bakes)
    , Meshes(meshes)
    , Materials(materials)
    , MaterialSets(materialSets)
    , Textures(textures)
    , SkinnedMeshes(skinnedMeshes)
    , Logging(logging)
    , Log(logging.GetLogger<SceneRenderQueueBuilder>())
{
}

SceneRenderQueueBuilder::~SceneRenderQueueBuilder() = default;

void SceneRenderQueueBuilder::Build(const EditorDocument& document)
{
    const EditorScene& scene = document.GetScene();
    const bool changed = Draws.Refresh(scene, Bakes, document.GetDefaultMaterial());
    if (changed || EmittedVersion != Draws.Version())
    {
        EmitBrushQueue();
        RebuildBrushCasters(document);
        EmittedVersion = Draws.Version();
    }
    const bool preview = PreviewEnabled && PreviewRegistry != nullptr;
    if (preview)
    {
        // The cooked snapshot replaces both solid queues: cells carry the
        // atlas, and placements carry their cooked scale/bias. Live geometry
        // still feeds the shadow casters below (it is the same geometry).
        EmitPreviewQueue();
        EmittedVersion = 0; // the brush queue must be re-emitted once the preview ends
        PlacedMeshes.Reset();
        PlacedMeshes.SortOpaque();
    }
    else
        BuildMeshQueue(document);
    BuildLights(document);
    BuildShadowCasters(document);

    // Probe volumes are cook inputs (they select the .sprobe lattice), so
    // editing one restales the badge like a brush or light edit does. No
    // visibility filter: the cook bakes hidden volumes too.
    uint64_t probeVolumesHash = kFnv1aOffsetBasis;
    const World& world = scene.GetRegistry().Components;
    if (world.IsRegistered<IrradianceVolumeComponent>())
    {
        world.ForEachComponent<IrradianceVolumeComponent>(
            [&](EntityId entity, const IrradianceVolumeComponent& volume)
            {
                const Transform3f* transform = scene.TryGetWorldTransform(entity);
                if (transform == nullptr)
                    return;
                HashFnv1aValue(probeVolumesHash, transform->Position);
                HashFnv1aValue(probeVolumesHash, volume);
            });
    }

    CurrentDocHash = Draws.ContentDigest() ^ (LightsHash * 0x9E3779B97F4A7C15ull)
        ^ (probeVolumesHash * 0xC2B2AE3D27D4EB4Full);
    PreviewStale = PreviewRegistry != nullptr && CurrentDocHash != PreviewDocHash;
}

void SceneRenderQueueBuilder::EmitBrushQueue()
{
    // Emission order is irrelevant to batching: SortOpaque keys on material,
    // mesh and section above depth, so every placement of one baked mesh under
    // one material lands in one instanced run. Runs only when the retained
    // draws changed; an unchanged frame keeps the sorted queue as is.
    Brushes.Reset();
    for (const BrushDrawEntity& entity : Draws.Entities())
    {
        for (const BrushMeshRun& run : entity.Runs)
        {
            const BrushDrawMesh& drawMesh = entity.Meshes[run.MeshIndex];
            const GpuStaticMesh* mesh = Meshes.Get(drawMesh.Handle);
            if (mesh == nullptr)
                continue;
            for (std::uint32_t i = 0; i < run.PlacementCount; ++i)
            {
                const BrushPlacement& placement = entity.Placements[run.FirstPlacement + i];
                MeshDrawInstance instance;
                instance.Mesh = drawMesh.Handle;
                instance.WorldMatrix = placement.World;
                instance.WorldBounds = placement.WorldBounds;
                EmitMeshSections(instance, *mesh, drawMesh.SlotMaterials, Materials, Brushes);
            }
        }
    }
    Brushes.SortOpaque();
}

void SceneRenderQueueBuilder::RebuildBrushCasters(const EditorDocument& document)
{
    // Every piece casts on its own: one item set per placement of a baked
    // mesh, at that placement's world matrix. The diff record is per brush
    // entity and is only the invalidation unit: its bounds are the union of
    // the entity's pieces, its mask their OR, and its state hash folds every
    // piece's mesh handle and material state, so a rebake of any one distinct
    // mesh or a placement change reads as a change over that entity's extent.
    BrushCasters.Reset();
    const Registry& registry = document.GetScene().GetRegistry();
    for (const BrushDrawEntity& entity : Draws.Entities())
    {
        ShadowCasterGatherResult folded;
        StaticMeshHandle firstMesh;
        for (const BrushMeshRun& run : entity.Runs)
        {
            const BrushDrawMesh& drawMesh = entity.Meshes[run.MeshIndex];
            const GpuStaticMesh* mesh = Meshes.Get(drawMesh.Handle);
            if (mesh == nullptr)
                continue;
            for (std::uint32_t i = 0; i < run.PlacementCount; ++i)
            {
                const BrushPlacement& placement = entity.Placements[run.FirstPlacement + i];
                const ShadowCasterGatherResult gathered = AppendShadowCasterSections(
                    drawMesh.Handle, *mesh, drawMesh.SlotMaterials, Materials,
                    ~0u, placement.World, placement.WorldBounds, BrushCasters);
                if (gathered.EffectiveSectionMask == 0)
                    continue;
                if (!firstMesh.IsValid())
                    firstMesh = drawMesh.Handle;
                folded.EffectiveSectionMask |= gathered.EffectiveSectionMask;
                HashFnv1aValue(folded.MaterialStateHash, gathered.MaterialStateHash);
                HashFnv1aValue(folded.MaterialStateHash, drawMesh.Handle.ToToken());
                folded.WorldBounds.ExpandToInclude(gathered.WorldBounds);
            }
        }
        if (folded.EffectiveSectionMask == 0)
            continue;
        // Cooked brush cells carry their materials in the mesh, not a set.
        AppendShadowCasterRecord(BrushCasters, MakeRenderEntityKey(registry, entity.Key.Entity),
                                 firstMesh, MaterialSetHandle{}, folded);
    }
}

void SceneRenderQueueBuilder::BuildMeshQueue(const EditorDocument& document)
{
    PlacedMeshes.Reset();

    const EditorScene& scene = document.GetScene();
    const World& world = scene.GetRegistry().Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityEffectivelyVisible(entity))
            continue;
        const StaticMeshComponent* renderer = world.TryGet<StaticMeshComponent>(entity);
        if (renderer == nullptr || !renderer->Visible)
            continue;

        const GpuStaticMesh* mesh = Meshes.Get(renderer->Mesh);
        const std::vector<MaterialHandle>* sectionMaterials = MaterialSets.Get(renderer->Materials);
        if (mesh == nullptr || sectionMaterials == nullptr || sectionMaterials->empty())
            continue;
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        if (transform == nullptr)
            continue;

        const Mat4 worldMatrix = transform->ToMat4();

        MeshDrawInstance instance;
        instance.Mesh = renderer->Mesh;
        instance.WorldMatrix = worldMatrix;
        instance.WorldBounds = TransformAabb(mesh->LocalBounds, worldMatrix);
        instance.SectionMask = renderer->SectionMask;
        EmitMeshSections(instance, *mesh, *sectionMaterials, Materials, PlacedMeshes);
    }

    // Skinned placements, at rest geometry through the same expansion the
    // runtime uses. No lightmap or AO stamp: a skinned mesh is the canonical
    // movable non-receiver. Without a skinned cache the loop emits nothing.
    if (SkinnedMeshes != nullptr)
    {
        for (const EntityId entity : scene.GetAllEntities())
        {
            if (!scene.IsEntityEffectivelyVisible(entity))
                continue;
            const SkinnedMeshComponent* renderer =
                world.TryGet<SkinnedMeshComponent>(entity);
            if (renderer == nullptr || !renderer->Visible)
                continue;

            const GpuStaticMesh* mesh = SkinnedMeshes->Get(renderer->Mesh);
            const std::vector<MaterialHandle>* sectionMaterials =
                MaterialSets.Get(renderer->Materials);
            if (mesh == nullptr || sectionMaterials == nullptr
                || sectionMaterials->empty())
                continue;
            const Transform3f* transform = scene.TryGetWorldTransform(entity);
            if (transform == nullptr)
                continue;

            const Mat4 worldMatrix = transform->ToMat4();

            MeshDrawInstance instance;
            instance.SkinnedMesh = renderer->Mesh;
            instance.WorldMatrix = worldMatrix;
            instance.WorldBounds = TransformAabb(mesh->LocalBounds, worldMatrix);
            instance.SectionMask = renderer->SectionMask;
            EmitMeshSections(instance, *mesh, *sectionMaterials, Materials, PlacedMeshes);
        }
    }
    PlacedMeshes.SortOpaque();
}

void SceneRenderQueueBuilder::SetLightmapPreview(const LightmapPreviewSource& source)
{
    PreviewRegistry.reset();

    std::ifstream file(source.CookedScenePath);
    if (!file.is_open())
    {
        Log.Error("lightmap preview: cannot open '{}'",
                  source.CookedScenePath.generic_string());
        return;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    JsonParseError parseError;
    const std::optional<JsonValue> json = JsonParse(buffer.str(), &parseError);
    if (!json)
    {
        Log.Error("lightmap preview: parse error in '{}': {}",
                  source.CookedScenePath.generic_string(), parseError.Message);
        return;
    }

    auto registry = std::make_unique<Registry>();
    InitializeSceneRegistry(*registry, Assets.Stores());
    // The engine vocabulary comes with the registry; a loaded module's does
    // not, and a scene naming one of its tags would refuse to load without it.
    InstallEditorModuleVocabulary(registry->Components);
    SceneSerializationContext context(Logging, &Assets);
    SceneLoadError loadError;
    if (!LoadSceneJson(*json, *registry, EditorSceneSerializers(), context, &loadError))
    {
        Log.Error("lightmap preview: scene load error: {}", loadError.Message);
        return;
    }

    // The cooked scene carries local transforms plus parentage; compose the
    // world transforms once. The snapshot never mutates, so one propagation is
    // exact for its lifetime -- the runtime does the same thing every frame.
    PropagateTransforms(registry->Components);

    PreviewRegistry = std::move(registry);
    PreviewDocHash = CurrentDocHash;
    PreviewStale = false;
    Log.Info("lightmap preview: loaded '{}'", source.CookedScenePath.generic_string());
}

void SceneRenderQueueBuilder::EmitPreviewQueue()
{
    Brushes.Reset();
    const World& world = PreviewRegistry->Components;

    // Per partition, through the same resolution the runtime uses. Collapsing
    // every zone's atlas to one pair of indices stamped whichever zone happened
    // to be visited last onto every other zone's meshes, so a multi-zone
    // preview disagreed with the game it is previewing.
    //
    // The filter set is built first because the runtime's collector takes the
    // frame's resident partitions, and a preview snapshot wants all of them.
    std::vector<ZoneLightmapIndices> lightmapTable;
    if (Textures != nullptr && world.IsRegistered<ZoneLightmapComponent>())
    {
        StoragePartitionSet lightmapPartitions;
        world.ForEachComponent<ZoneLightmapComponent>(
            [&](EntityId entity, const ZoneLightmapComponent&)
            {
                lightmapPartitions.Add(world.GetEntityPartition(entity));
            });

        std::vector<ZoneLightmapBinding> bindings;
        std::vector<std::pair<StoragePartitionId, ZoneLightmapIndices>> resolved;
        CollectZoneLightmaps(world, lightmapPartitions, bindings);
        ResolveZoneLightmapIndices(bindings, *Textures, resolved);
        BuildZoneLightmapTable(resolved, lightmapTable);
    }

    if (world.IsRegistered<StaticMeshComponent>() && world.IsRegistered<WorldTransform>())
        world.ForEachComponent<StaticMeshComponent>(
            [&](EntityId entity, const StaticMeshComponent& renderer)
            {
                if (!renderer.Visible)
                    return;
                const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                const GpuStaticMesh* mesh = Meshes.Get(renderer.Mesh);
                const std::vector<MaterialHandle>* sectionMaterials =
                    MaterialSets.Get(renderer.Materials);
                if (transform == nullptr || mesh == nullptr
                    || sectionMaterials == nullptr || sectionMaterials->empty())
                    return;

                const Mat4 worldMatrix = transform->Value.ToMat4();
                const ZoneLightmapIndices lightmap = LookupZoneLightmap(
                    lightmapTable, world.GetEntityPartition(entity));

                MeshDrawInstance instance;
                instance.Mesh = renderer.Mesh;
                instance.WorldMatrix = worldMatrix;
                instance.WorldBounds = TransformAabb(mesh->LocalBounds, worldMatrix);
                instance.SectionMask = renderer.SectionMask;
                instance.LightmapTextureIndex = lightmap.Lightmap;
                instance.AoTextureIndex = lightmap.Ao;
                instance.LightmapScaleBias = renderer.LightmapScaleBias;
                EmitMeshSections(instance, *mesh, *sectionMaterials, Materials, Brushes);
            });

    // Skinned placements appear in the preview too: the cooked scene carries
    // them (the runtime draws them), and a character vanishing when the
    // preview toggles would misrepresent the cook. No lightmap stamp.
    if (SkinnedMeshes != nullptr && world.IsRegistered<SkinnedMeshComponent>()
        && world.IsRegistered<WorldTransform>())
        world.ForEachComponent<SkinnedMeshComponent>(
            [&](EntityId entity, const SkinnedMeshComponent& renderer)
            {
                if (!renderer.Visible)
                    return;
                const WorldTransform* transform = world.TryGet<WorldTransform>(entity);
                const GpuStaticMesh* mesh = SkinnedMeshes->Get(renderer.Mesh);
                const std::vector<MaterialHandle>* sectionMaterials =
                    MaterialSets.Get(renderer.Materials);
                if (transform == nullptr || mesh == nullptr
                    || sectionMaterials == nullptr || sectionMaterials->empty())
                    return;

                const Mat4 worldMatrix = transform->Value.ToMat4();

                MeshDrawInstance instance;
                instance.SkinnedMesh = renderer.Mesh;
                instance.WorldMatrix = worldMatrix;
                instance.WorldBounds = TransformAabb(mesh->LocalBounds, worldMatrix);
                instance.SectionMask = renderer.SectionMask;
                EmitMeshSections(instance, *mesh, *sectionMaterials, Materials, Brushes);
            });
    Brushes.SortOpaque();
}

void SceneRenderQueueBuilder::BuildLights(const EditorDocument& document)
{
    // Reset() clears only the packed counts; the ambient tints and shadow
    // tunables are owned by the caller (EditorRenderFeature stamps them from
    // render.* cvars before Build), so we leave them untouched here. The
    // shadow-view gather below reads ShadowSoftness, which is why the stamp
    // has to happen first.
    SceneLights.Reset();
    LightSelectionCurrent = false;
    EditorLightGather gathered = GatherEditorLights(
        document, SceneLights.ShadowSoftness);
    LightCandidates = std::move(gathered.Candidates);
    LightsHash = gathered.ContentHash;
}

std::span<const SpotShadowRequest> SceneRenderQueueBuilder::BuildShadowRequests(
    const Vec<3>& viewOrigin)
{
    SelectForwardLights(LightCandidates, viewOrigin, SceneLights,
                        ShadowRequests, PointShadowRequests);
    LightSelectionCurrent = true;
    return ShadowRequests;
}

std::span<const PointShadowRequest> SceneRenderQueueBuilder::BuildPointShadowRequests(
    const Vec<3>& viewOrigin)
{
    if (!LightSelectionCurrent)
    {
        SelectForwardLights(LightCandidates, viewOrigin, SceneLights,
                            ShadowRequests, PointShadowRequests);
        LightSelectionCurrent = true;
    }
    return PointShadowRequests;
}

void SceneRenderQueueBuilder::BuildShadowCasters(const EditorDocument& document)
{
    SceneCasters.Reset();

    const EditorScene& scene = document.GetScene();
    const Registry& registry = scene.GetRegistry();

    // The brush casters are retained (RebuildBrushCasters) and bulk-copied
    // here: plain items and records, no geometry recomputed per frame.
    SceneCasters.Items.insert(SceneCasters.Items.end(), BrushCasters.Items.begin(),
                              BrushCasters.Items.end());
    SceneCasters.Records.insert(SceneCasters.Records.end(), BrushCasters.Records.begin(),
                                BrushCasters.Records.end());

    const World& world = registry.Components;
    for (const EntityId entity : scene.GetAllEntities())
    {
        if (!scene.IsEntityEffectivelyVisible(entity))
            continue;
        const StaticMeshComponent* renderer = world.TryGet<StaticMeshComponent>(entity);
        if (renderer == nullptr)
            continue;
        const GpuStaticMesh* mesh = Meshes.Get(renderer->Mesh);
        const std::vector<MaterialHandle>* sectionMaterials =
            MaterialSets.Get(renderer->Materials);
        const Transform3f* transform = scene.TryGetWorldTransform(entity);
        if (mesh == nullptr || sectionMaterials == nullptr || transform == nullptr)
            continue;

        const ShadowCasterGatherResult gathered = AppendShadowCasters(
            *renderer, *mesh, *sectionMaterials, Materials,
            transform->ToMat4(), SceneCasters);
        if (gathered.EffectiveSectionMask == 0)
            continue;

        AppendShadowCasterRecord(SceneCasters, MakeRenderEntityKey(registry, entity),
                                 renderer->Mesh, renderer->Materials, gathered);
    }
}
