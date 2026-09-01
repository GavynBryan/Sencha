#include "SceneRenderQueueBuilder.h"
#include "EditorRenderEntityKey.h"

#include "document/BrushCookInput.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "render/EditorLightGather.h"

#include <assets/cook/BrushClustering.h>   // CookBrushGeometry
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <assets/cook/BrushGeometryCook.h> // CollectMaterialOrder, BakeBrushFacesToStaticMesh
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

namespace
{
    // Content hash of the collected brushes: the bake is skipped (no GPU upload)
    // when this is unchanged. Covers each face's material path and the input
    // vertices (position/normal/uv); tangents are bake output, not input.
    uint64_t HashBrushes(const std::vector<CookBrushGeometry>& brushes)
    {
        uint64_t h = kFnv1aOffsetBasis;
        for (const CookBrushGeometry& brush : brushes)
        {
            for (const CookFace& face : brush.Faces)
            {
                HashFnv1aBytes(h, face.Material.Path.data(), face.Material.Path.size());
                for (const StaticMeshVertex& v : face.Triangles)
                {
                    HashFnv1aValue(h, v.Position);
                    HashFnv1aValue(h, v.Normal);
                    HashFnv1aValue(h, v.Uv0);
                }
            }
            HashFnv1aByte(h, '|'); // brush boundary, so regrouping faces changes the hash
        }
        return h;
    }

}

SceneRenderQueueBuilder::SceneRenderQueueBuilder(AssetSystem& assets,
                                                 StaticMeshCache& meshes,
                                                 MaterialCache& materials,
                                                 MaterialSetCache& materialSets,
                                                 LoggingProvider& logging,
                                                 TextureCache* textures,
                                                 SkinnedMeshCache* skinnedMeshes,
                                                 AnimationClipCache* animationClips)
    : Assets(assets)
    , Meshes(meshes)
    , Materials(materials)
    , MaterialSets(materialSets)
    , Textures(textures)
    , SkinnedMeshes(skinnedMeshes)
    , AnimationClips(animationClips)
    , Logging(logging)
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
    const bool preview = PreviewEnabled && PreviewRegistry != nullptr;
    if (preview)
    {
        // The cooked snapshot replaces both solid queues: cells carry the
        // atlas, and placements carry their cooked scale/bias. Live geometry
        // still feeds the shadow casters below (it is the same geometry).
        EmitPreviewQueue();
        PlacedMeshes.Reset();
        PlacedMeshes.SortOpaque();
    }
    else
    {
        EmitBrushQueue();
        BuildMeshQueue(document);
    }
    BuildLights(document);
    BuildShadowCasters(document);

    // Probe volumes are cook inputs (they select the .sprobe lattice), so
    // editing one restales the badge like a brush or light edit does. No
    // visibility filter: the cook bakes hidden volumes too.
    uint64_t probeVolumesHash = kFnv1aOffsetBasis;
    const EditorScene& scene = document.GetScene();
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

    CurrentDocHash = BrushHash ^ (LightsHash * 0x9E3779B97F4A7C15ull)
        ^ (probeVolumesHash * 0xC2B2AE3D27D4EB4Full);
    PreviewStale = PreviewRegistry != nullptr && CurrentDocHash != PreviewDocHash;
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

        // Brush geometry is baked in world space (BrushTessellate), so it sits
        // at identity. Its section bounds are already world-space, which the
        // shared emit cannot express through one instance-wide box, so each
        // section is emitted on its own.
        for (uint32_t section = 0; section < static_cast<uint32_t>(mesh->Sections.size()); ++section)
        {
            MeshDrawInstance instance;
            instance.Mesh = entry.Mesh;
            instance.WorldMatrix = Mat4::Identity();
            instance.WorldBounds = mesh->Sections[section].LocalBounds;
            instance.SectionMask = 1u << section;
            EmitMeshSections(instance, *mesh, entry.SlotMaterials, Materials, Brushes);
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
    InitializeSceneRegistry(*registry, &Meshes, &MaterialSets,
                            nullptr, nullptr, nullptr, Textures, SkinnedMeshes,
                            AnimationClips);
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
        // Cooked brush cells carry their materials in the mesh, not a set.
        AppendShadowCasterRecord(SceneCasters, key, entry.Mesh,
                                 MaterialSetHandle{}, gathered);
    }

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

void SceneRenderQueueBuilder::ReleaseBrushMeshes()
{
    for (const CachedBrushMesh& entry : BrushMeshes)
        Meshes.Destroy(entry.Mesh);
    BrushMeshes.clear();

    for (const MaterialHandle material : BrushMaterials)
        Assets.ReleaseMaterial(material);
    BrushMaterials.clear();
}
