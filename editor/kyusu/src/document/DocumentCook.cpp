#include "DocumentCook.h"

#include "BrushCookInput.h"
#include "EditorDocument.h"

#include <assets/cook/BakeBvh.h>
#include <assets/cook/BrushClustering.h>
#include <assets/cook/BrushGeometryCook.h>
#include <assets/cook/CollisionShapeCook.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/DirectLightBake.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/cook/LightmapAtlasPack.h>
#include <assets/cook/LightmapRaster.h>
#include <assets/cook/ProbeBake.h>
#include <assets/cook/SceneCookOutput.h>
#include <assets/cook/TextureCook.h>
#include <assets/probes/ProbeVolumeFormat.h>
#include <assets/static_mesh/MeshLoader.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <assets/texture/TextureSerializer.h>
#include <core/assets/AssetSystem.h>
#include <math/spatial/GridTransform3d.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/LightComponentTypes.h>
#include <render/PointLightComponent.h>
#include <render/RenderLight.h>
#include <render/SpotLightComponent.h>
#include <world/transform/TransformComponents.h>
#include <core/assets/AssetIdMap.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>
#include <core/assets/RuntimeAssets.h>
#include <core/hash/ContentHash.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/MeshGeometry.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    // The bake-input fingerprint: exactly the data that reaches the bake (each
    // face's resolved material plus its world-space triangles) and the cell
    // size. Non-geometry component edits don't touch it, so they don't force a
    // re-bake (05-§3); a real geometry/material/transform change does, because
    // the collected triangles are post-transform.
    uint64_t HashBrushInputs(std::span<const CookBrushGeometry> brushes, double cellSize)
    {
        uint64_t h = HashBytes64(std::as_bytes(std::span<const double>{ &cellSize, 1 }));
        for (const CookBrushGeometry& brush : brushes)
            for (const CookFace& face : brush.Faces)
            {
                h = HashBytes64(face.Material.Path, h);
                h = HashBytes64(std::as_bytes(std::span<const StaticMeshVertex>{
                    face.Triangles.data(), face.Triangles.size() }), h);
                // Chart identity and chart-space UVs are hashed explicitly: a
                // soft-edge toggle between coplanar faces changes charts (so
                // the lightmap layout) without moving a single vertex byte.
                h = HashBytes64(std::as_bytes(std::span<const std::uint32_t>{
                    &face.Chart, 1 }), h);
                if (!face.ChartUv.empty())
                    h = HashBytes64(std::as_bytes(std::span<const Vec2d>{
                        face.ChartUv.data(), face.ChartUv.size() }), h);
            }
        return h;
    }

    // Unorm16 lightmap UV component (texel-center convention).
    std::uint16_t PackLightmapUv16(float value)
    {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
    }

    // Fold the baked-direct light set into the cook hash so moving, recoloring,
    // or retuning a Direct light restales the level and re-bakes the vertices.
    uint64_t HashDirectLights(std::span<const BakeDirectLight> lights, uint64_t seed)
    {
        uint64_t h = seed;
        for (const BakeDirectLight& light : lights)
        {
            const float values[] = {
                light.Position.X, light.Position.Y, light.Position.Z,
                light.Color.X, light.Color.Y, light.Color.Z,
                light.Intensity, light.Range,
                light.Direction.X, light.Direction.Y, light.Direction.Z,
                light.ConeScale, light.ConeOffset,
            };
            h = HashBytes64(std::as_bytes(std::span<const float>{ values, std::size(values) }), h);
            const std::uint8_t kind = static_cast<std::uint8_t>(light.Kind);
            h = HashBytes64(std::as_bytes(std::span<const std::uint8_t>{ &kind, 1 }), h);
        }
        return h;
    }

    std::string CellBase(const Vec3i& coord)
    {
        return "cell_" + std::to_string(coord.X) + "_" + std::to_string(coord.Y)
            + "_" + std::to_string(coord.Z);
    }

    std::string CellName(const Vec3i& coord) { return CellBase(coord) + ".smesh"; }

    // Flatten a cell's already-triangulated faces into a position/index soup for
    // the collision bake (cell-local, the same triangles the render mesh uses).
    void CollectCellTriangles(const std::vector<CookFace>& faces,
                              std::vector<Vec3d>& positions,
                              std::vector<uint32_t>& indices)
    {
        for (const CookFace& face : faces)
            for (const StaticMeshVertex& vertex : face.Triangles)
            {
                indices.push_back(static_cast<uint32_t>(positions.size()));
                positions.push_back(vertex.Position);
            }
    }

    // One cell's cooked collision: the blob's path (relative to the cooked root)
    // and the cell origin the runtime places the static collider at.
    struct CollisionEntry
    {
        std::string BlobRelPath;
        Vec3d Origin;
    };
} // namespace

JsonValue BuildCellEntity(const Vec3d& origin,
                          std::string_view meshPath,
                          std::span<const AssetRef> materials)
{
    JsonValue::Object local{
        { "position", JsonValue(JsonValue::Array{
            JsonValue(static_cast<double>(origin.X)),
            JsonValue(static_cast<double>(origin.Y)),
            JsonValue(static_cast<double>(origin.Z)) }) },
        { "rotation", JsonValue(JsonValue::Array{
            JsonValue(0.0), JsonValue(0.0), JsonValue(0.0), JsonValue(1.0) }) },
        { "scale", JsonValue(JsonValue::Array{
            JsonValue(1.0), JsonValue(1.0), JsonValue(1.0) }) },
    };

    JsonValue::Array materialPaths;
    materialPaths.reserve(materials.size());
    for (const AssetRef& material : materials)
        materialPaths.push_back(JsonValue(material.Path));

    JsonValue::Object staticMesh{
        { "mesh", JsonValue(std::string(meshPath)) },
        { "materials", JsonValue(std::move(materialPaths)) },
        { "visible", JsonValue(true) },
        { "layer_mask", JsonValue(static_cast<double>(0xFFFFFFFFu)) },
        { "section_mask", JsonValue(static_cast<double>(0xFFFFFFFFu)) },
    };

    return JsonValue(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{
            { "Transform", JsonValue(JsonValue::Object{ { "local", JsonValue(std::move(local)) } }) },
            { "StaticMesh", JsonValue(std::move(staticMesh)) },
        }) },
    });
}

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

// An authored probe volume resolved to its world lattice. StableIndex is the
// collection order (entity iteration order, deterministic per document), the
// runtime's overlap tiebreaker.
struct ProbeVolumeInput
{
    GridTransform3d Grid;
    std::int32_t Priority = 0;
};

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

uint64_t HashProbeVolumes(std::span<const ProbeVolumeInput> volumes, uint64_t seed)
{
    uint64_t h = seed;
    for (const ProbeVolumeInput& volume : volumes)
    {
        const float values[] = {
            volume.Grid.Origin.X, volume.Grid.Origin.Y, volume.Grid.Origin.Z,
            volume.Grid.CellSize,
            static_cast<float>(volume.Grid.DimsX),
            static_cast<float>(volume.Grid.DimsY),
            static_cast<float>(volume.Grid.DimsZ),
            static_cast<float>(volume.Priority),
        };
        h = HashBytes64(std::as_bytes(std::span<const float>{ values, std::size(values) }), h);
    }
    return h;
}

// A placed instanceable mesh joining the zone's lightmap: its cook-document
// entity (for the scale/bias writeback), placement transform, CPU geometry
// (sheet UVs in LightmapU/V), and the atlas rect sizing derived from it.
struct LightmapPlacement
{
    EntityId Entity;
    Mat4 ToWorld = Mat4::Identity();
    MeshGeometry Geometry;
    Vec2d WorldExtent;               // world size the [0,1] sheet spans, per axis
    std::uint32_t Chart = 0;         // index into the shared atlas layout
    bool CastsIntoBake = true;       // AffectsBakedLighting: occludes others
};

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
                    const float world =
                        (placement.ToWorld.TransformPoint(b.Position)
                         - placement.ToWorld.TransformPoint(a.Position)).Magnitude();
                    const float su = std::abs(b.LightmapU - a.LightmapU) / 65535.0f;
                    const float sv = std::abs(b.LightmapV - a.LightmapV) / 65535.0f;
                    if (su > 1e-5f)
                        du = std::max(du, world / su);
                    if (sv > 1e-5f)
                        dv = std::max(dv, world / sv);
                }
            if (du <= 0.0f && dv <= 0.0f)
                return;
            placement.WorldExtent = Vec2d{ std::max(du, 1e-3f), std::max(dv, 1e-3f) };
            placements.push_back(std::move(placement));
        });
    return placements;
}

// The cook kernel. Operates on a mutable document it is free to mutate
// destructively (it drops brush entities before serializing the passthrough
// scene), so both callers hand it a throwaway: the file cook loads one from
// disk, the live cook snapshots the editor's document. stem names the level's
// artifacts; sourceRel is the cooked-cache source key.
DocumentCookResult CookDocumentKernel(EditorDocument& doc,
                                  std::string_view stem,
                                  std::string_view sourceRel,
                                  const std::filesystem::path& assetsRoot,
                                  double cellSize,
                                  LoggingProvider& logging,
                                  AssetSystem* assetSystem,
                                  const LightingCookParams& lightmapParams)
{
    DocumentCookResult result;

    // Lights first: charts are generated only when something will bake into
    // them. Collect -> hash -> cluster; the hash is taken on the collected
    // input (including chart identity) so it reflects exactly what gets
    // baked. Each bake's tuning is hashed only when its inputs exist, since
    // the output is otherwise independent of it.
    const std::vector<BakeDirectLight> bakeLights =
        CollectBakeLights(doc.GetRegistry().Components, false);
    const std::vector<ProbeVolumeInput> probeVolumes =
        CollectProbeVolumes(doc.GetRegistry().Components);
    // Probes bounce off surfaces lit by Direct AND Indirect lights: Indirect
    // is the authoring contract for "dynamic direct, baked mood".
    const std::vector<BakeDirectLight> bounceLights = probeVolumes.empty()
        ? std::vector<BakeDirectLight>{}
        : CollectBakeLights(doc.GetRegistry().Components, true);
    CookChartSet charts;
    std::vector<CookBrushGeometry> brushes = CollectCookBrushes(
        doc.GetScene(), doc.GetDefaultMaterial(),
        bakeLights.empty() ? nullptr : &charts,
        lightmapParams.ConeDegrees, lightmapParams.LuxelSize);
    // Placements matter to the lightmap (they receive rects) and to probes
    // (they occlude rays), so they are collected when either bake runs.
    std::vector<LightmapPlacement> placements =
        (bakeLights.empty() && probeVolumes.empty())
        ? std::vector<LightmapPlacement>{}
        : CollectLightmapPlacements(doc.GetRegistry().Components, assetSystem,
                                    assetsRoot, logging);
    uint64_t geometryHash =
        HashDirectLights(bakeLights, HashBrushInputs(brushes, cellSize));
    if (!bakeLights.empty() || !probeVolumes.empty())
    {
        for (const LightmapPlacement& placement : placements)
        {
            geometryHash = HashBytes64(std::as_bytes(std::span<const Mat4>{
                &placement.ToWorld, 1 }), geometryHash);
            geometryHash = HashBytes64(std::as_bytes(std::span<const StaticMeshVertex>{
                placement.Geometry.Vertices.data(),
                placement.Geometry.Vertices.size() }), geometryHash);
            const std::uint8_t casts = placement.CastsIntoBake ? 1 : 0;
            geometryHash = HashBytes64(std::as_bytes(std::span<const std::uint8_t>{
                &casts, 1 }), geometryHash);
        }
    }
    if (!bakeLights.empty())
    {
        const float tuning[] = {
            lightmapParams.Shading.DiffuseWrap, lightmapParams.Shading.NormalOffset,
            lightmapParams.LuxelSize, static_cast<float>(lightmapParams.MaxAtlasSize),
            lightmapParams.ConeDegrees,
        };
        geometryHash = HashBytes64(
            std::as_bytes(std::span<const float>{ tuning, std::size(tuning) }),
            geometryHash);
    }
    if (!probeVolumes.empty())
    {
        geometryHash = HashDirectLights(bounceLights, geometryHash);
        geometryHash = HashProbeVolumes(probeVolumes, geometryHash);
        const float tuning[] = {
            lightmapParams.Probe.BounceAlbedo,
            lightmapParams.Probe.SkyColor.X, lightmapParams.Probe.SkyColor.Y,
            lightmapParams.Probe.SkyColor.Z,
            lightmapParams.Probe.GroundColor.X, lightmapParams.Probe.GroundColor.Y,
            lightmapParams.Probe.GroundColor.Z,
            lightmapParams.Probe.MaxRayDistance,
            lightmapParams.Probe.ClassifyRayDistance,
            lightmapParams.Probe.ClassifyBackfaceRatio,
            lightmapParams.Shading.DiffuseWrap, lightmapParams.Shading.NormalOffset,
            static_cast<float>(lightmapParams.ProbeRayCount),
        };
        geometryHash = HashBytes64(
            std::as_bytes(std::span<const float>{ tuning, std::size(tuning) }),
            geometryHash);
    }
    std::vector<BrushCell> cells = ClusterBrushesIntoCells(brushes, cellSize);

    // Pack the atlas and write final atlas UVs into the cell vertices BEFORE
    // the per-cell mesh bake: the weld compares whole vertices, so identical
    // UVs weld chart interiors and differing UVs split chart borders, with no
    // dedicated chart-splitting logic.
    LightmapAtlasLayout atlasLayout;
    if (!bakeLights.empty())
    {
        // Placements pack into the same zone atlas as the brush charts: one
        // rect per placement, sized by the world span its [0,1] sheet covers.
        std::vector<Vec2d> extents = charts.Extents;
        for (LightmapPlacement& placement : placements)
        {
            placement.Chart = static_cast<std::uint32_t>(extents.size());
            extents.push_back(placement.WorldExtent);
        }
        atlasLayout = PackLightmapAtlas(
            extents, lightmapParams.LuxelSize, lightmapParams.MaxAtlasSize);
        const float luxel = atlasLayout.EffectiveLuxelSize;

        // Per-placement scale/bias: the mesh's sheet UVs map linearly into
        // its rect's grid points; the runtime applies uv * xy + zw. Written
        // into the cook document's component so SaveSceneJson serializes it.
        for (const LightmapPlacement& placement : placements)
        {
            const LightmapChartRect& rect = atlasLayout.Rects[placement.Chart];
            const float pointsU = std::ceil(placement.WorldExtent.X / luxel);
            const float pointsV = std::ceil(placement.WorldExtent.Y / luxel);
            StaticMeshComponent* renderer =
                doc.GetScene().GetRegistry().Components.TryGet<StaticMeshComponent>(
                    placement.Entity);
            if (renderer == nullptr)
                continue;
            renderer->LightmapScaleBias = Vec4{
                pointsU / static_cast<float>(atlasLayout.Width),
                pointsV / static_cast<float>(atlasLayout.Height),
                (static_cast<float>(rect.X + kLightmapGutter) + 0.5f)
                    / static_cast<float>(atlasLayout.Width),
                (static_cast<float>(rect.Y + kLightmapGutter) + 0.5f)
                    / static_cast<float>(atlasLayout.Height) };
        }
        for (BrushCell& cell : cells)
            for (CookFace& face : cell.Faces)
            {
                if (face.Chart >= atlasLayout.Rects.size())
                    continue;
                const LightmapChartRect& rect = atlasLayout.Rects[face.Chart];
                for (std::size_t i = 0; i < face.Triangles.size()
                     && i < face.ChartUv.size(); ++i)
                {
                    // Grid point k maps to the CENTER of texel (rect + gutter
                    // + k); bilinear filtering then interpolates exactly
                    // between adjacent grid samples.
                    const float u = (static_cast<float>(rect.X + kLightmapGutter)
                        + face.ChartUv[i].X / luxel + 0.5f)
                        / static_cast<float>(atlasLayout.Width);
                    const float v = (static_cast<float>(rect.Y + kLightmapGutter)
                        + face.ChartUv[i].Y / luxel + 0.5f)
                        / static_cast<float>(atlasLayout.Height);
                    face.Triangles[i].LightmapU = PackLightmapUv16(u);
                    face.Triangles[i].LightmapV = PackLightmapUv16(v);
                }
            }
        if (luxel != lightmapParams.LuxelSize)
            logging.GetLogger<EditorDocument>().Warn(
                "cook: lightmap atlas overflowed {}x{}; luxel size clamped {} -> {}",
                lightmapParams.MaxAtlasSize, lightmapParams.MaxAtlasSize,
                lightmapParams.LuxelSize, luxel);
    }

    const std::string stemStr(stem);
    MeshSerializer serializer(logging);

    JsonValue::Array cellEntities;
    std::vector<CookedArtifact> artifacts;
    std::vector<std::string> materialRefs; // distinct face materials, in cook order
    std::unordered_set<std::string> seenMaterial;
    std::unordered_set<std::string> generatedMeshPaths;
    std::vector<CollisionEntry> collisionEntries;

    // Cell meshes are written after the lighting bake, not inline: the bake
    // needs every cell's geometry (for the shared occlusion BVH) before any
    // cell's channel is final. Origin is the cell's world translation.
    struct PendingCellMesh
    {
        std::filesystem::path Physical;
        MeshGeometry Geometry;
        Vec3d Origin;
    };
    std::vector<PendingCellMesh> pendingMeshes;

    for (const BrushCell& cell : cells)
    {
        std::vector<AssetRef> order = CollectMaterialOrder(cell.Faces);

        MeshGeometry geometry;
        std::string bakeError;
        if (!BakeBrushFacesToStaticMesh(cell.Faces, order, geometry, &bakeError))
        {
            result.Error = "CookDocument: " + bakeError;
            return result;
        }

        const std::string cellName = CellName(cell.Coord);
        const std::string meshAssetPath = "asset://levels/" + stemStr + "/" + cellName;
        const std::string meshRelPath = ".cooked/levels/" + stemStr + "/" + cellName;
        const std::filesystem::path meshPhysical = assetsRoot / meshRelPath;

        std::error_code ec;
        std::filesystem::create_directories(meshPhysical.parent_path(), ec);
        pendingMeshes.push_back(PendingCellMesh{
            meshPhysical, std::move(geometry), cell.Origin });

        cellEntities.push_back(BuildCellEntity(cell.Origin, meshAssetPath, order));
        artifacts.push_back(CookedArtifact{ meshAssetPath, meshRelPath, AssetType::StaticMesh });
        result.GeneratedMeshPaths.push_back(meshAssetPath);
        generatedMeshPaths.insert(meshAssetPath);
        for (const AssetRef& material : order)
            if (seenMaterial.insert(material.Path).second)
                materialRefs.push_back(material.Path);

        // Collision: bake the same cell triangles into a pre-baked Jolt blob, a
        // sibling of the cell mesh. Authored brushes become collidable with no
        // collider authoring; the runtime loads these from the sidecar at map load.
        std::vector<Vec3d> collisionPositions;
        std::vector<uint32_t> collisionIndices;
        CollectCellTriangles(cell.Faces, collisionPositions, collisionIndices);
        const std::vector<std::byte> collisionBlob =
            BakeCollisionBlob(collisionPositions, collisionIndices);
        if (!collisionBlob.empty())
        {
            const std::string colRel = "levels/" + stemStr + "/" + CellBase(cell.Coord) + ".scol";
            const std::filesystem::path colPhysical = assetsRoot / ".cooked" / colRel;
            std::ofstream colFile(colPhysical, std::ios::binary);
            colFile.write(reinterpret_cast<const char*>(collisionBlob.data()),
                          static_cast<std::streamsize>(collisionBlob.size()));
            if (colFile.good())
            {
                collisionEntries.push_back(CollisionEntry{ colRel, cell.Origin });
                artifacts.push_back(
                    CookedArtifact{ "asset://" + colRel, ".cooked/" + colRel, AssetType::Collision });
            }
        }
    }

    // One occlusion BVH over every cell's world triangles serves both bakes:
    // a light in one cell shadows onto its neighbors, and probe rays see the
    // whole document. No cross-zone halo yet (single-document cook).
    BakeBvh occlusionBvh;
    if (!bakeLights.empty() || !probeVolumes.empty())
    {
        std::vector<BakeTriangle> occluders;
        for (const PendingCellMesh& pending : pendingMeshes)
        {
            const Mat4 toWorld = Mat4::MakeTranslation(pending.Origin);
            const MeshGeometry& geometry = pending.Geometry;
            for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
            {
                occluders.push_back(BakeTriangle{
                    toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i]].Position),
                    toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i + 1]].Position),
                    toWorld.TransformPoint(geometry.Vertices[geometry.Indices[i + 2]].Position) });
            }
        }
        // Placed meshes occlude too (and shadow each other) unless authored
        // out via AffectsBakedLighting.
        for (const LightmapPlacement& placement : placements)
        {
            if (!placement.CastsIntoBake)
                continue;
            const MeshGeometry& geometry = placement.Geometry;
            for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
            {
                occluders.push_back(BakeTriangle{
                    placement.ToWorld.TransformPoint(
                        geometry.Vertices[geometry.Indices[i]].Position),
                    placement.ToWorld.TransformPoint(
                        geometry.Vertices[geometry.Indices[i + 1]].Position),
                    placement.ToWorld.TransformPoint(
                        geometry.Vertices[geometry.Indices[i + 2]].Position) });
            }
        }
        occlusionBvh.Build(std::move(occluders));
    }

    // Bake static direct lighting into the zone's lightmap atlas. The lights
    // were collected up front (folded into the cook hash).
    if (!bakeLights.empty())
    {
        result.DirectLightCount = bakeLights.size();
        result.LightmapAtlasWidth = atlasLayout.Width;
        result.LightmapAtlasHeight = atlasLayout.Height;
        result.EffectiveLuxelSize = atlasLayout.EffectiveLuxelSize;

        // Gather each chart's triangles (world positions + smoothed normals +
        // chart grid UVs) from the pre-weld cell faces, then rasterize and
        // bake chart by chart.
        std::vector<std::vector<LightmapRasterTriangle>> chartTriangles(
            atlasLayout.Rects.size());
        const float luxel = atlasLayout.EffectiveLuxelSize;
        for (const BrushCell& cell : cells)
            for (const CookFace& face : cell.Faces)
            {
                if (face.Chart >= chartTriangles.size())
                    continue;
                for (std::size_t i = 0; i + 2 < face.Triangles.size()
                     && i + 2 < face.ChartUv.size(); i += 3)
                {
                    LightmapRasterTriangle tri;
                    for (int k = 0; k < 3; ++k)
                    {
                        tri.Uv[k] = Vec2d{ face.ChartUv[i + k].X / luxel,
                                           face.ChartUv[i + k].Y / luxel };
                        tri.Position[k] = face.Triangles[i + k].Position + cell.Origin;
                        tri.Normal[k] = face.Triangles[i + k].Normal;
                    }
                    chartTriangles[face.Chart].push_back(tri);
                }
            }

        // Placement charts: sheet UVs scale into the rect's grid points; the
        // luxel bake then runs on the placed world triangles like any chart.
        for (const LightmapPlacement& placement : placements)
        {
            const float pointsU = std::ceil(placement.WorldExtent.X / luxel);
            const float pointsV = std::ceil(placement.WorldExtent.Y / luxel);
            const MeshGeometry& geometry = placement.Geometry;
            if (placement.Chart >= chartTriangles.size())
                continue;
            std::vector<LightmapRasterTriangle>& out = chartTriangles[placement.Chart];
            for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
            {
                LightmapRasterTriangle tri;
                for (int k = 0; k < 3; ++k)
                {
                    const StaticMeshVertex& vertex = geometry.Vertices[geometry.Indices[i + k]];
                    tri.Uv[k] = Vec2d{ vertex.LightmapU / 65535.0f * pointsU,
                                       vertex.LightmapV / 65535.0f * pointsV };
                    tri.Position[k] = placement.ToWorld.TransformPoint(vertex.Position);
                    tri.Normal[k] =
                        placement.ToWorld.TransformVector(vertex.Normal).Normalized();
                }
                out.push_back(tri);
            }
        }

        std::vector<std::uint32_t> atlasPixels(
            static_cast<std::size_t>(atlasLayout.Width) * atlasLayout.Height, 0u);
        for (std::size_t c = 0; c < chartTriangles.size(); ++c)
            BakeChartLuxels(chartTriangles[c], atlasLayout.Rects[c], bakeLights,
                            occlusionBvh, lightmapParams.Shading,
                            atlasLayout.Width, atlasPixels);

        // Emit the atlas as a cooked texture artifact plus the zone component
        // that binds it at runtime.
        TextureData atlas;
        atlas.Format = TexturePixelFormat::RGBA8;
        atlas.Usage = TextureUsage::LinearData;
        atlas.Filter = TextureFilter::Linear;
        atlas.Width = atlasLayout.Width;
        atlas.Height = atlasLayout.Height;
        atlas.Mips = { TextureMipLevel{ atlasLayout.Width, atlasLayout.Height, 0,
                                        atlasPixels.size() * sizeof(std::uint32_t) } };
        atlas.Blob.resize(atlasPixels.size() * sizeof(std::uint32_t));
        std::memcpy(atlas.Blob.data(), atlasPixels.data(), atlas.Blob.size());

        const std::string atlasRel = "levels/" + stemStr + "/lightmap.stex";
        const std::string atlasAssetPath = "asset://" + atlasRel;
        TextureSerializer textureSerializer(logging);
        if (!textureSerializer.WriteToFile(
                (assetsRoot / ".cooked" / atlasRel).generic_string(), atlas))
        {
            result.Error = "CookDocument: could not write lightmap atlas '" + atlasRel + "'";
            return result;
        }
        artifacts.push_back(CookedArtifact{
            atlasAssetPath, ".cooked/" + atlasRel, AssetType::Texture });
        generatedMeshPaths.insert(atlasAssetPath);
        materialRefs.push_back(atlasAssetPath);
        cellEntities.push_back(JsonValue(JsonValue::Object{
            { "components", JsonValue(JsonValue::Object{
                { "ZoneLightmap", JsonValue(JsonValue::Object{
                    { "texture", JsonValue(atlasAssetPath) },
                }) },
            }) },
        }));
    }

    // Bake authored irradiance-probe volumes into the zone's .sprobe. Bounce
    // comes from Direct and Indirect lights against the same occlusion BVH;
    // the runtime locates the file by the cooked-scene path convention.
    if (!probeVolumes.empty())
    {
        ProbeBakeParams probeParams = lightmapParams.Probe;
        probeParams.Shading = lightmapParams.Shading;
        const std::vector<Vec3d> rayTable =
            BuildProbeRayTable(lightmapParams.ProbeRayCount);

        ProbeVolumeFile probeFile;
        std::size_t probeCount = 0;
        for (std::size_t i = 0; i < probeVolumes.size(); ++i)
        {
            const ProbeVolumeInput& input = probeVolumes[i];
            const ProbeVolumeBakeResult baked = BakeProbeVolume(
                input.Grid, occlusionBvh, bounceLights, rayTable, probeParams);

            ProbeVolumeRecord record;
            record.Grid = input.Grid;
            record.Priority = input.Priority;
            record.StableIndex = static_cast<std::uint32_t>(i);
            record.ShHalf = PackProbeShHalf(baked.Sh);
            record.ValidityBits = PackValidityBits(baked.Valid);
            probeCount += baked.Sh.size();
            probeFile.Volumes.push_back(std::move(record));
        }

        const std::string probeRel = "levels/" + stemStr + "/probes.sprobe";
        const std::filesystem::path probePhysical = assetsRoot / ".cooked" / probeRel;
        std::error_code makeDirs;
        std::filesystem::create_directories(probePhysical.parent_path(), makeDirs);
        std::ofstream probeStream(probePhysical, std::ios::binary | std::ios::trunc);
        BinaryWriter probeWriter(probeStream);
        if (!probeStream.is_open() || !WriteProbeVolumeFile(probeWriter, probeFile))
        {
            result.Error = "CookDocument: could not write probe volumes '" + probeRel + "'";
            return result;
        }
        artifacts.push_back(CookedArtifact{
            "asset://" + probeRel, ".cooked/" + probeRel, AssetType::ProbeVolume });
        result.ProbeVolumeCount = probeVolumes.size();
        result.ProbeCount = probeCount;
    }

    for (const PendingCellMesh& pending : pendingMeshes)
    {
        if (!serializer.WriteToFile(pending.Physical.generic_string(), pending.Geometry))
        {
            result.Error = "CookDocument: could not write mesh '"
                + pending.Physical.generic_string() + "'";
            return result;
        }
    }

    // Drop the brush entities so SaveSceneJson emits only passthrough game
    // components (the cook is the one and only StaticMeshComponent emitter), then
    // append the cell entities. Baked entities pass through as ordinary props,
    // but their editor-only dormant-source annotation is stripped: the cooked
    // scene never carries editor data.
    {
        EditorScene& scene = doc.GetScene();
        std::vector<EntityId> brushEntities;
        for (EntityId entity : scene.GetAllEntities())
        {
            if (scene.TryGetBrush(entity) != nullptr)
            {
                brushEntities.push_back(entity);
                continue;
            }
            if (scene.TryGetBakedBrush(entity) != nullptr)
                scene.GetRegistry().Components.RemoveComponent<BakedBrushComponent>(entity);
        }
        for (EntityId entity : brushEntities)
            scene.DestroyEntity(entity);
    }

    // Serialize passthrough entities through the shared asset system so prop
    // StaticMesh handles emit asset:// paths (the cell entities are raw JSON and
    // bypass the codec, but authored props go through it). A null assetSystem is
    // the brush-only cook (no asset fields to resolve).
    SceneSerializationContext context(logging, assetSystem);
    JsonValue cooked = SaveSceneJson(doc.GetRegistry(), context);
    bool appended = false;
    if (cooked.IsObject())
        for (auto& [key, value] : cooked.AsObject())
            if (key == "entities" && value.IsArray())
            {
                for (JsonValue& cellEntity : cellEntities)
                    value.AsArray().push_back(std::move(cellEntity));
                appended = true;
                break;
            }
    if (!appended)
    {
        result.Error = "CookDocument: assembled scene has no entities array";
        return result;
    }

    std::error_code ec;
    const std::filesystem::path cookedDir = assetsRoot / ".cooked/levels";
    std::filesystem::create_directories(cookedDir, ec);
    const std::filesystem::path cookedScenePath = cookedDir / (stemStr + ".cooked.json");
    const std::filesystem::path manifestPath = cookedDir / (stemStr + ".manifest.json");

    // Collision sidecar: the runtime loads this at map load (LoadZoneCollision)
    // to spawn the level's static brush colliders. Empty array if no brushes.
    {
        JsonValue::Array sidecar;
        sidecar.reserve(collisionEntries.size());
        for (const CollisionEntry& entry : collisionEntries)
            sidecar.push_back(JsonValue(JsonValue::Object{
                { "blob", JsonValue(entry.BlobRelPath) },
                { "origin", JsonValue(JsonValue::Array{
                    JsonValue(static_cast<double>(entry.Origin.X)),
                    JsonValue(static_cast<double>(entry.Origin.Y)),
                    JsonValue(static_cast<double>(entry.Origin.Z)) }) },
            }));
        std::ofstream sidecarFile(cookedDir / (stemStr + ".collision.json"));
        sidecarFile << JsonStringify(JsonValue(std::move(sidecar)), /*pretty*/ true);
    }

    // asset:// resolution: Generated cell meshes live under .cooked/; every other
    // ref (materials, their textures) is an authored asset under the root.
    const auto physicalPathFor =
        [&assetsRoot, &generatedMeshPaths](std::string_view assetPath) -> std::filesystem::path {
            constexpr std::string_view prefix = "asset://";
            const std::string rel(assetPath.substr(prefix.size()));
            if (generatedMeshPaths.count(std::string(assetPath)) != 0)
                return assetsRoot / ".cooked" / rel;
            return assetsRoot / rel;
        };

    std::string cookError;
    if (!WriteCookedScene(cooked, materialRefs, physicalPathFor,
            assetsRoot / kAssetIdMapFileName, manifestPath, cookedScenePath, &cookError))
    {
        result.Error = "CookDocument: " + cookError;
        return result;
    }

    // Cook source textures the level's materials reference (.png -> .stex) into
    // .cooked/ so the COOK=OFF player can load them. The import driver maintains
    // the cooked index keyed by source path; the index.Put below loads that
    // updated index and adds the level entry, so both survive. Idempotent:
    // unchanged sources are served from the cooked cache.
    {
        PngTextureImporter textureImporter;
        AssetImporterRegistry importers;
        importers.Register(textureImporter);
        AssetRegistry scratch(logging); // we want the on-disk artifacts + index, not a live registry
        (void)ImportAssetsOnDemand(assetsRoot.generic_string(), importers, scratch, logging);
    }

    // Record source -> artifacts (source key = caller-supplied rel path, hash key
    // = brush-geometry hash).
    const std::filesystem::path indexPath = assetsRoot / ".cooked/index.json";
    CookedCacheIndex index;
    (void)CookedCacheIndex::LoadFromFile(indexPath.generic_string(), index); // cold cache is fine
    CookedSourceEntry entry;
    entry.SourceRelPath = std::string(sourceRel);
    entry.SourceHash = geometryHash;
    entry.Artifacts = std::move(artifacts);
    index.Put(std::move(entry));
    (void)index.SaveToFile(indexPath.generic_string());

    result.Success = true;
    result.CookedScenePath = cookedScenePath;
    result.ManifestPath = manifestPath;
    result.CollisionSidecarPath = cookedDir / (stemStr + ".collision.json");
    result.ContentHash = geometryHash;
    result.CellCount = cells.size();
    return result;
}
} // namespace

DocumentCookResult CookDocument(const std::filesystem::path& authoredLevelPath,
                          const std::filesystem::path& assetsRoot,
                          double cellSize,
                          LoggingProvider* logging,
                          RuntimeAssets* assets,
                          const LightingCookParams& lightmapParams)
{
    // A sink-less local logger keeps the no-logging call headless and silent.
    LoggingProvider silent;
    LoggingProvider& log = logging != nullptr ? *logging : silent;
    EditorDocument doc(log);
    if (assets != nullptr)
        doc.SetAssetEnvironment(*assets);
    if (!doc.Load(authoredLevelPath.generic_string()))
    {
        DocumentCookResult result;
        result.Error = "CookDocument: could not load '" + authoredLevelPath.generic_string() + "'";
        return result;
    }

    std::error_code ec;
    const std::string sourceRel =
        std::filesystem::relative(authoredLevelPath, assetsRoot, ec).generic_string();
    return CookDocumentKernel(doc, authoredLevelPath.stem().generic_string(), sourceRel,
                             assetsRoot, cellSize, log, assets != nullptr ? &assets->Assets : nullptr,
                             lightmapParams);
}

DocumentCookResult CookDocument(const EditorDocument& liveDocument,
                          std::string_view levelName,
                          const std::filesystem::path& assetsRoot,
                          double cellSize,
                          LoggingProvider& logging,
                          RuntimeAssets* assets,
                          const LightingCookParams& lightmapParams)
{
    // Snapshot the live (possibly unsaved) document into a throwaway the kernel is
    // free to mutate, leaving the editor's document untouched. The snapshot shares
    // the same asset system so its prop handles round-trip through ToJson/LoadFromJson.
    EditorDocument snapshot(logging);
    if (assets != nullptr)
        snapshot.SetAssetEnvironment(*assets);
    if (!snapshot.LoadFromJson(liveDocument.ToJson()))
    {
        DocumentCookResult result;
        result.Error = "CookDocument: could not snapshot the live document";
        return result;
    }

    const std::string sourceRel = "levels/" + std::string(levelName) + ".level.json";
    return CookDocumentKernel(snapshot, levelName, sourceRel, assetsRoot, cellSize,
                             logging, assets != nullptr ? &assets->Assets : nullptr,
                             lightmapParams);
}
