#include "DocumentCookInputData.h"

#include "BakeTriangleGather.h"
#include "EditorDocument.h"

#include <assets/static_mesh/MeshLoader.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/LightGpuTypes.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/scene/SceneInstance.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "DocumentSerialization.h"

namespace
{
// Lights resolved to world-space bake inputs. Direct lights feed the lightmap
// (their diffuse bakes there and they leave the runtime forward set, so a
// bake is the only thing that makes them visible); with includeIndirect the
// set widens to Indirect lights, whose direct stays dynamic but whose bounce
// feeds the probe bake. Spot cone parameters reuse the runtime packing so the
// baked cone matches the shader cone exactly.
std::vector<BakeDirectLight> CollectBakeLights(const World& world, bool includeIndirect)
{
    const auto contributes = [includeIndirect](LightBakeContribution bake)
    {
        return bake == LightBakeContribution::Direct
            || (includeIndirect && bake == LightBakeContribution::Indirect);
    };

    std::vector<BakeDirectLight> lights;
    if (!world.IsRegistered<LocalTransform>())
        return lights;

    if (world.IsRegistered<PointLightComponent>())
    {
        world.ForEachComponent<PointLightComponent>(
            [&](EntityId entity, const PointLightComponent& light)
            {
                if (!contributes(light.BakeContribution))
                    return;
                const LocalTransform* transform = world.TryGet<LocalTransform>(entity);
                if (transform == nullptr)
                    return;
                BakeDirectLight baked{};
                baked.Kind = BakeLightKind::Point;
                baked.Position = transform->Value.Position;
                baked.Color = light.Color;
                baked.Intensity = light.Intensity;
                baked.Range = light.Range;
                lights.push_back(baked);
            });
    }

    if (world.IsRegistered<SpotLightComponent>())
    {
        world.ForEachComponent<SpotLightComponent>(
            [&](EntityId entity, const SpotLightComponent& light)
            {
                if (!contributes(light.BakeContribution))
                    return;
                const LocalTransform* transform = world.TryGet<LocalTransform>(entity);
                if (transform == nullptr)
                    return;
                const Vec<3> direction = transform->Value.Forward();
                const GpuLight packed =
                    MakeSpotGpuLight(transform->Value.Position, direction, light);
                BakeDirectLight baked{};
                baked.Kind = BakeLightKind::Spot;
                baked.Position = transform->Value.Position;
                baked.Color = light.Color;
                baked.Intensity = light.Intensity;
                baked.Range = light.Range;
                baked.Direction = Vec3d(packed.DirectionCone.X,
                                        packed.DirectionCone.Y,
                                        packed.DirectionCone.Z);
                baked.ConeScale = packed.ConeScale;
                baked.ConeOffset = packed.ConeOffset;
                lights.push_back(baked);
            });
    }

    return lights;
}

std::vector<ProbeVolumeInput> CollectProbeVolumes(const World& world)
{
    std::vector<ProbeVolumeInput> volumes;
    if (!world.IsRegistered<LocalTransform>()
        || !world.IsRegistered<IrradianceVolumeComponent>())
        return volumes;

    world.ForEachComponent<IrradianceVolumeComponent>(
        [&](EntityId entity, const IrradianceVolumeComponent& volume)
        {
            const LocalTransform* transform = world.TryGet<LocalTransform>(entity);
            if (transform == nullptr || volume.CellSize <= 0.0f)
                return;
            ProbeVolumeInput input;
            input.Grid = GridTransform3d::FromBounds(
                Aabb3d::FromCenterHalfExtent(transform->Value.Position,
                                             volume.HalfExtents),
                volume.CellSize);
            input.Priority = volume.Priority;
            volumes.push_back(input);
        });
    return volumes;
}

// Gathers placed StaticMesh entities whose mesh asset carries lightmap sheet
// UVs. Needs the asset system (handle -> path) and disk access (the cook
// loads CPU geometry; the runtime caches hold none); a null assetSystem cooks
// brush-only and bakes no placements. Deterministic: entity iteration order.
std::vector<LightmapPlacement> CollectLightmapPlacements(
    const World& world, AssetSystem* assetSystem,
    const std::filesystem::path& assetsRoot, LoggingProvider& logging)
{
    std::vector<LightmapPlacement> placements;
    if (assetSystem == nullptr || !world.IsRegistered<StaticMeshComponent>()
        || !world.IsRegistered<LocalTransform>())
        return placements;

    MeshLoader loader(logging);
    world.ForEachComponent<StaticMeshComponent>(
        [&](EntityId entity, const StaticMeshComponent& renderer)
        {
            const LocalTransform* transform = world.TryGet<LocalTransform>(entity);
            if (transform == nullptr)
                return;
            const std::string_view assetPath =
                assetSystem->GetPathForStaticMesh(renderer.Mesh);
            constexpr std::string_view prefix = "asset://";
            if (assetPath.size() <= prefix.size())
                return;
            const std::string rel(assetPath.substr(prefix.size()));

            // Authored meshes live under the root; generated ones under
            // .cooked (the same split physicalPathFor uses for refs).
            std::filesystem::path physical = assetsRoot / rel;
            std::error_code ec;
            if (!std::filesystem::exists(physical, ec))
                physical = assetsRoot / ".cooked" / rel;

            LightmapPlacement placement;
            if (!loader.LoadFromFile(physical.generic_string(), placement.Geometry))
                return;

            bool hasSheet = false;
            for (const StaticMeshVertex& vertex : placement.Geometry.Vertices)
                if (vertex.LightmapU != 0 || vertex.LightmapV != 0)
                {
                    hasSheet = true;
                    break;
                }
            if (!hasSheet)
                return;

            placement.Entity = entity;
            placement.ToWorld = transform->Value.ToMat4();
            placement.CastsIntoBake = renderer.AffectsBakedLighting;

            // World density of the sheet: the steepest world-per-sheet-unit
            // ratio over the indexed edges, per axis, measured on the PLACED
            // triangles so instance scale is included.
            float du = 0.0f;
            float dv = 0.0f;
            const std::vector<StaticMeshVertex>& verts = placement.Geometry.Vertices;
            const std::vector<uint32_t>& indices = placement.Geometry.Indices;
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                for (int e = 0; e < 3; ++e)
                {
                    const StaticMeshVertex& a = verts[indices[i + e]];
                    const StaticMeshVertex& b = verts[indices[i + (e + 1) % 3]];
                    const float worldLength =
                        (placement.ToWorld.TransformPoint(b.Position)
                         - placement.ToWorld.TransformPoint(a.Position)).Magnitude();
                    const float su = std::abs(b.LightmapU - a.LightmapU) / 65535.0f;
                    const float sv = std::abs(b.LightmapV - a.LightmapV) / 65535.0f;
                    if (su > 1e-5f)
                        du = std::max(du, worldLength / su);
                    if (sv > 1e-5f)
                        dv = std::max(dv, worldLength / sv);
                }
            if (du <= 0.0f && dv <= 0.0f)
                return;
            placement.WorldExtent = Vec2d{ std::max(du, 1e-3f), std::max(dv, 1e-3f) };
            placements.push_back(std::move(placement));
        });
    return placements;
}

JsonValue* FindMutable(JsonValue& value, std::string_view key)
{
    if (!value.IsObject())
        return nullptr;
    for (auto& [field, child] : value.AsObject())
        if (field == key)
            return &child;
    return nullptr;
}

JsonValue BuildPassthroughScene(const EditorDocument& document,
                                LoggingProvider& logging,
                                AssetSystem* assets,
                                std::unordered_map<EntityIndex, std::uint32_t>& sceneIndices)
{
    SceneSerializationContext context(logging, assets);
    JsonValue serialized =
        SaveSceneJson(document.GetRegistry(), EditorSceneSerializers(), context);
    const JsonValue* sourceEntities = serialized.Find("entities");
    const JsonValue* sourceHierarchy = serialized.Find("hierarchy");
    if (sourceEntities == nullptr || !sourceEntities->IsArray()
        || sourceHierarchy == nullptr || !sourceHierarchy->IsArray())
        return {};

    const std::vector<EntityId> alive =
        document.GetRegistry().Components.GetAliveEntities();
    if (alive.size() != sourceEntities->AsArray().size())
        return {};

    std::vector<std::uint32_t> remap(alive.size(), UINT32_MAX);
    JsonValue::Array entities;
    for (std::size_t oldIndex = 0; oldIndex < alive.size(); ++oldIndex)
    {
        const EntityId entity = alive[oldIndex];
        if (document.GetScene().TryGetBrush(entity) != nullptr)
            continue;

        remap[oldIndex] = static_cast<std::uint32_t>(entities.size());
        sceneIndices.emplace(entity.Index, remap[oldIndex]);
        JsonValue entityJson = sourceEntities->AsArray()[oldIndex];
        if (JsonValue* components = FindMutable(entityJson, "components");
            components != nullptr && components->IsObject())
        {
            // Editor-only annotations: the bake linkage and the authored
            // display name. Neither exists in the runtime schema, and an
            // unknown component key fails the zone import by design.
            std::erase_if(components->AsObject(),
                [](const auto& field)
                { return field.first == "baked_brush" || field.first == "name"; });

            // Expanded placement members carry their instance identity into
            // the cooked scene (locked decision D1): the placement id, and
            // the source as its asset path -- the cook stamps it to a stable
            // id exactly like every other ref.
            if (const SceneInstanceId owner = document.SceneInstanceOwnerOf(entity);
                owner.IsValid())
            {
                components->AsObject().push_back({ "scene_instance",
                    JsonValue(JsonValue::Object{
                        { "source", JsonValue(document.SceneInstanceSourceOf(entity)) },
                        { "id", JsonValue(SceneInstanceIdToString(owner)) },
                    }) });
            }
        }
        entities.push_back(std::move(entityJson));
    }

    JsonValue::Array hierarchy;
    for (const JsonValue& relation : sourceHierarchy->AsArray())
    {
        const JsonValue* child = relation.Find("child");
        const JsonValue* parent = relation.Find("parent");
        if (child == nullptr || !child->IsNumber()
            || parent == nullptr || !parent->IsNumber())
            continue;
        const std::uint32_t oldChild = static_cast<std::uint32_t>(child->AsNumber());
        const std::uint32_t oldParent = static_cast<std::uint32_t>(parent->AsNumber());
        if (oldChild >= remap.size() || oldParent >= remap.size()
            || remap[oldChild] == UINT32_MAX || remap[oldParent] == UINT32_MAX)
            continue;
        hierarchy.push_back(JsonValue(JsonValue::Object{
            { "child", JsonValue(static_cast<double>(remap[oldChild])) },
            { "parent", JsonValue(static_cast<double>(remap[oldParent])) },
        }));
    }

    return JsonValue(JsonValue::Object{
        { "version", JsonValue(1.0) },
        { "entities", JsonValue(std::move(entities)) },
        { "hierarchy", JsonValue(std::move(hierarchy)) },
    });
}
} // namespace

DocumentCookInput::DocumentCookInput(std::unique_ptr<Data> data)
    : Input(std::move(data))
{
}

DocumentCookInput::DocumentCookInput(DocumentCookInput&&) noexcept = default;
DocumentCookInput& DocumentCookInput::operator=(DocumentCookInput&&) noexcept = default;
DocumentCookInput::~DocumentCookInput() = default;

const DocumentCookRequest& DocumentCookInput::Request() const { return Input->Request; }
const DocumentCookSnapshot& DocumentCookInput::Snapshot() const { return Input->Snapshot; }

std::optional<DocumentCookInput> CollectDocumentCookInput(
    const EditorDocument& document,
    const std::filesystem::path& assetsRoot,
    double cellSize,
    LoggingProvider& logging,
    RuntimeAssets* assets,
    const LightingCookParams& lightmapParams,
    std::span<const ProbeHaloZone> halo,
    const DocumentCookOptions& options,
    std::string* error)
{
    // The cook consumes the live registry, and the projection is part of it.
    // Anything unresolved -- a source that failed, a path with no recorded id,
    // an override aimed at nothing -- would cook a scene that silently differs
    // from what the placement means, so it refuses instead.
    if (const auto& projection = document.GetProjectionDiagnostics();
        !projection.Clean())
    {
        std::string message = "scene instances did not fully resolve:";
        if (!projection.ResolveError.empty())
            message += " " + projection.ResolveError;
        for (const std::string& missing : projection.MissingIds)
            message += " [missing id] " + missing;
        for (const std::string& dangling : projection.DanglingOverrides)
            message += " [dangling override] " + dangling;
        if (error != nullptr)
            *error = message;
        return std::nullopt;
    }

    const auto fail = [error](std::string message) -> std::optional<DocumentCookInput>
    {
        if (error != nullptr)
            *error = std::move(message);
        return std::nullopt;
    };

    auto data = std::make_unique<DocumentCookInput::Data>();
    DocumentCookRequest& request = data->Request;
    DocumentCookSnapshot& snapshot = data->Snapshot;

    request.Profile = options.Profile != nullptr
        ? *options.Profile
        : BuiltInCookProfiles().front();
    request.Graph = ResolveDocumentCookGraph(request.Profile);
    if (!request.Graph.Valid)
        return fail("invalid cook profile: " + request.Graph.Error);
    request.ForceRebuild = options.ForceRebuild;
    request.OutputNamespace = options.OutputNamespace;
    request.Control = options.Control;
    request.CellSize = cellSize;

    // Local selection flags drive lazy collection: gather only what the resolved
    // graph runs and the profile publishes. Selection itself lives in the graph
    // (request.Selects), never in a stored Run* boolean.
    const auto selected = [&request](std::string_view step, std::string_view family)
    {
        return request.Selects(step)
            && request.Disposition(family) != CookOutputDisposition::Withdraw;
    };
    const bool selectDirect =
        selected(CookStepIds::DirectLightmap, CookOutputFamilies::DirectLightmap);
    const bool selectAo =
        selected(CookStepIds::AmbientOcclusion, CookOutputFamilies::AmbientOcclusion);
    const bool selectProbe =
        selected(CookStepIds::IrradianceProbes, CookOutputFamilies::IrradianceProbes);

    snapshot.Lighting = lightmapParams;
    snapshot.Lighting.Ao.Enabled = lightmapParams.Ao.Enabled && selectAo;
    snapshot.Halo.assign(halo.begin(), halo.end());

    AssetSystem* assetSystem = assets != nullptr ? &assets->Assets : nullptr;
    std::unordered_map<EntityIndex, std::uint32_t> sceneIndices;
    snapshot.PassthroughScene = BuildPassthroughScene(
        document, logging, assetSystem, sceneIndices);
    if (!snapshot.PassthroughScene.IsObject())
        return fail("could not serialize passthrough scene");

    snapshot.BakeLights = (selectDirect || selectAo)
        ? CollectBakeLights(document.GetRegistry().Components, false)
        : std::vector<BakeDirectLight>{};
    snapshot.ProbeVolumes = selectProbe
        ? CollectProbeVolumes(document.GetRegistry().Components)
        : std::vector<ProbeVolumeInput>{};
    snapshot.BounceLights = snapshot.ProbeVolumes.empty()
        ? std::vector<BakeDirectLight>{}
        : CollectBakeLights(document.GetRegistry().Components, true);
    snapshot.Brushes = CollectCookBrushes(
        document.GetScene(), document.GetDefaultMaterial(),
        snapshot.BakeLights.empty() ? nullptr : &snapshot.Charts,
        snapshot.Lighting.ConeDegrees, snapshot.Lighting.LuxelSize);
    snapshot.Placements = (snapshot.BakeLights.empty() && snapshot.ProbeVolumes.empty())
        ? std::vector<LightmapPlacement>{}
        : CollectLightmapPlacements(document.GetRegistry().Components, assetSystem,
                                    assetsRoot, logging);
    for (LightmapPlacement& placement : snapshot.Placements)
    {
        const auto found = sceneIndices.find(placement.Entity.Index);
        if (found == sceneIndices.end())
            return fail("placed mesh is missing from passthrough scene");
        placement.SceneEntityIndex = found->second;
    }

    return DocumentCookInput(std::move(data));
}

std::optional<ProbeHaloZone> CollectZoneBakeHalo(
    const std::filesystem::path& authoredLevelPath,
    const std::filesystem::path& assetsRoot,
    LoggingProvider* logging,
    RuntimeAssets* assets)
{
    LoggingProvider silent;
    LoggingProvider& log = logging != nullptr ? *logging : silent;
    EditorDocument doc(log);
    doc.SetContentRoots({ assetsRoot });
    if (assets != nullptr)
        doc.SetAssetEnvironment(*assets);
    if (!doc.Load(authoredLevelPath.generic_string()))
        return std::nullopt;

    // Pre-weld brush face triangles are geometrically the surfaces the zone's
    // own cook bakes into cell meshes (the weld dedupes vertices, it does not
    // move them), so neighbors occlude against the same geometry the zone
    // renders.
    const std::vector<CookBrushGeometry> brushes =
        CollectCookBrushes(doc.GetScene(), doc.GetDefaultMaterial());
    const std::vector<LightmapPlacement> placements = CollectLightmapPlacements(
        doc.GetRegistry().Components,
        assets != nullptr ? &assets->Assets : nullptr, assetsRoot, log);

    ProbeHaloZone zone;
    for (const CookBrushGeometry& brush : brushes)
        for (const CookFace& face : brush.Faces)
            for (std::size_t i = 0; i + 2 < face.Triangles.size(); i += 3)
                zone.Triangles.push_back(BakeTriangle{
                    face.Triangles[i].Position,
                    face.Triangles[i + 1].Position,
                    face.Triangles[i + 2].Position });
    for (const LightmapPlacement& placement : placements)
        if (placement.CastsIntoBake)
            GatherWorldTriangles(placement.Geometry, placement.ToWorld, zone.Triangles);

    for (const BakeTriangle& triangle : zone.Triangles)
    {
        zone.Bounds.ExpandToInclude(triangle.V0);
        zone.Bounds.ExpandToInclude(triangle.V1);
        zone.Bounds.ExpandToInclude(triangle.V2);
    }
    zone.ContentHash = HashBytes64(std::as_bytes(std::span<const BakeTriangle>{
        zone.Triangles.data(), zone.Triangles.size() }));
    return zone;
}

