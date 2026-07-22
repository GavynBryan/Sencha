#include "DocumentCookFingerprints.h"

#include <assets/cook/CookFingerprint.h>
#include <core/hash/ContentHash.h>
#include <core/json/JsonStringify.h>
#include <render/static_mesh/MeshGeometry.h>

#include <string>
#include <string_view>
#include <vector>

namespace
{
    void AddVec2(CookFingerprint& fingerprint, std::string_view field, const Vec2d& value)
    {
        fingerprint.AddFloat(std::string(field) + ".x", value.X)
            .AddFloat(std::string(field) + ".y", value.Y);
    }

    void AddVec3(CookFingerprint& fingerprint, std::string_view field, const Vec3d& value)
    {
        fingerprint.AddFloat(std::string(field) + ".x", value.X)
            .AddFloat(std::string(field) + ".y", value.Y)
            .AddFloat(std::string(field) + ".z", value.Z);
    }

    void AddVertex(CookFingerprint& fingerprint, const StaticMeshVertex& vertex)
    {
        AddVec3(fingerprint, "position", vertex.Position);
        AddVec3(fingerprint, "normal", vertex.Normal);
        AddVec2(fingerprint, "uv0", vertex.Uv0);
        fingerprint.AddFloat("tangent.x", vertex.Tangent.X)
            .AddFloat("tangent.y", vertex.Tangent.Y)
            .AddFloat("tangent.z", vertex.Tangent.Z)
            .AddFloat("tangent.w", vertex.Tangent.W)
            .AddU32("lightmap_u", vertex.LightmapU)
            .AddU32("lightmap_v", vertex.LightmapV);
    }

    void AddMat4(CookFingerprint& fingerprint, std::string_view field, const Mat4& value)
    {
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                fingerprint.AddFloat(
                    std::string(field) + "." + std::to_string(row)
                        + "." + std::to_string(column),
                    value[row][column]);
    }

    // The brush identity covers resolved materials, post-transform triangles,
    // chart topology, and chart coordinates. Editor-only state never enters it.
    uint64_t HashBrushInputs(std::span<const CookBrushGeometry> brushes, double cellSize)
    {
        CookFingerprint fingerprint("brush_cells", 2);
        fingerprint.AddDouble("cell_size", cellSize)
            .AddU64("brush_count", brushes.size());
        for (const CookBrushGeometry& brush : brushes)
        {
            fingerprint.AddU64("face_count", brush.Faces.size());
            for (const CookFace& face : brush.Faces)
            {
                fingerprint.AddString("material", face.Material.Path)
                    .AddU32("chart", face.Chart)
                    .AddU64("vertex_count", face.Triangles.size());
                for (const StaticMeshVertex& vertex : face.Triangles)
                    AddVertex(fingerprint, vertex);
                fingerprint.AddU64("chart_uv_count", face.ChartUv.size());
                for (const Vec2d& uv : face.ChartUv)
                    AddVec2(fingerprint, "chart_uv", uv);
            }
        }
        return fingerprint.Value();
    }

    uint64_t HashDirectLights(std::span<const BakeDirectLight> lights)
    {
        CookFingerprint fingerprint("bake_direct_lights", 2);
        fingerprint.AddU64("light_count", lights.size());
        for (const BakeDirectLight& light : lights)
        {
            fingerprint.AddU32("kind", static_cast<std::uint32_t>(light.Kind));
            AddVec3(fingerprint, "position", light.Position);
            AddVec3(fingerprint, "color", light.Color);
            fingerprint.AddFloat("intensity", light.Intensity)
                .AddFloat("range", light.Range);
            AddVec3(fingerprint, "direction", light.Direction);
            fingerprint.AddFloat("cone_scale", light.ConeScale)
                .AddFloat("cone_offset", light.ConeOffset);
        }
        return fingerprint.Value();
    }

    uint64_t HashProbeVolumes(std::span<const ProbeVolumeInput> volumes)
    {
        CookFingerprint fingerprint("probe_volumes", 2);
        fingerprint.AddU64("volume_count", volumes.size());
        for (const ProbeVolumeInput& volume : volumes)
        {
            AddVec3(fingerprint, "origin", volume.Grid.Origin);
            fingerprint.AddFloat("cell_size", volume.Grid.CellSize)
                .AddU32("dims_x", volume.Grid.DimsX)
                .AddU32("dims_y", volume.Grid.DimsY)
                .AddU32("dims_z", volume.Grid.DimsZ)
                .AddI32("priority", volume.Priority);
        }
        return fingerprint.Value();
    }
} // namespace

DocumentCookFingerprints ComputeDocumentCookFingerprints(
    const DocumentCookSnapshot& snapshot,
    double cellSize,
    std::span<const ProbeHaloZone* const> reachableHalo)
{
    const LightingCookParams& lightmapParams = snapshot.Lighting;
    const std::vector<CookBrushGeometry>& brushes = snapshot.Brushes;
    const std::vector<LightmapPlacement>& placements = snapshot.Placements;
    const std::vector<BakeDirectLight>& bakeLights = snapshot.BakeLights;
    const std::vector<BakeDirectLight>& bounceLights = snapshot.BounceLights;
    const std::vector<ProbeVolumeInput>& probeVolumes = snapshot.ProbeVolumes;

    DocumentCookFingerprints fp;
    fp.Brush = HashBrushInputs(brushes, cellSize);

    CookFingerprint placementFingerprint("lightmap_placements", 2);
    placementFingerprint.AddU64("placement_count", placements.size());
    for (const LightmapPlacement& placement : placements)
    {
        AddMat4(placementFingerprint, "to_world", placement.ToWorld);
        placementFingerprint.AddBool("casts_into_bake", placement.CastsIntoBake)
            .AddU64("vertex_count", placement.Geometry.Vertices.size());
        for (const StaticMeshVertex& vertex : placement.Geometry.Vertices)
            AddVertex(placementFingerprint, vertex);
        placementFingerprint.AddU64("index_count", placement.Geometry.Indices.size());
        for (std::uint32_t index : placement.Geometry.Indices)
            placementFingerprint.AddU32("index", index);
    }
    fp.Placements = placementFingerprint.Value();

    CookFingerprint lightmapSurfaceIdentity("lightmap_surfaces", 1);
    lightmapSurfaceIdentity.AddDependency("brush_cells", fp.Brush)
        .AddDependency("placements", fp.Placements)
        .AddFloat("luxel_size", lightmapParams.LuxelSize)
        .AddU32("max_atlas_size", lightmapParams.MaxAtlasSize)
        .AddFloat("cone_degrees", lightmapParams.ConeDegrees);
    fp.LightmapSurfaces = lightmapSurfaceIdentity.Value();

    CookFingerprint occlusionIdentity("occlusion_geometry", 1);
    occlusionIdentity.AddDependency("brush_cells", fp.Brush)
        .AddDependency("placements", fp.Placements)
        .AddU64("reachable_halo_count", reachableHalo.size());
    for (const ProbeHaloZone* zone : reachableHalo)
        occlusionIdentity.AddU64("reachable_halo", zone->ContentHash);
    fp.Occlusion = occlusionIdentity.Value();

    CookFingerprint directIdentity("direct_lightmap", 1);
    directIdentity.AddDependency("lightmap_surfaces", fp.LightmapSurfaces)
        .AddDependency("occlusion_geometry", fp.Occlusion)
        .AddDependency("direct_lights", HashDirectLights(bakeLights))
        .AddFloat("diffuse_wrap", lightmapParams.Shading.DiffuseWrap)
        .AddFloat("normal_offset", lightmapParams.Shading.NormalOffset);
    fp.Direct = directIdentity.Value();

    CookFingerprint aoIdentity("ambient_occlusion", 1);
    aoIdentity.AddDependency("direct_lightmap", fp.Direct)
        .AddFloat("max_distance", lightmapParams.Ao.MaxDistance)
        .AddU32("ray_count", lightmapParams.Ao.RayCount);
    fp.Ao = aoIdentity.Value();

    CookFingerprint probeIdentity("irradiance_probes", 1);
    probeIdentity.AddDependency("occlusion_geometry", fp.Occlusion)
        .AddDependency("bounce_lights", HashDirectLights(bounceLights))
        .AddDependency("probe_volumes", HashProbeVolumes(probeVolumes))
        .AddFloat("bounce_albedo", lightmapParams.Probe.BounceAlbedo)
        .AddFloat("max_ray_distance", lightmapParams.Probe.MaxRayDistance)
        .AddFloat("classify_ray_distance", lightmapParams.Probe.ClassifyRayDistance)
        .AddFloat("classify_backface_ratio", lightmapParams.Probe.ClassifyBackfaceRatio)
        .AddFloat("diffuse_wrap", lightmapParams.Shading.DiffuseWrap)
        .AddFloat("normal_offset", lightmapParams.Shading.NormalOffset)
        .AddU32("ray_count", lightmapParams.ProbeRayCount);
    AddVec3(probeIdentity, "sky", lightmapParams.Probe.SkyColor);
    AddVec3(probeIdentity, "ground", lightmapParams.Probe.GroundColor);
    fp.Probe = probeIdentity.Value();

    CookFingerprint documentFingerprint("document_cook", 8);
    documentFingerprint.AddDependency("brush_cells", fp.Brush)
        .AddDependency("placements", fp.Placements)
        .AddDependency("direct_lights", HashDirectLights(bakeLights))
        .AddU64("passthrough_scene",
                HashBytes64(JsonStringify(snapshot.PassthroughScene, false)));
    if (!bakeLights.empty())
    {
        documentFingerprint.AddFloat("diffuse_wrap", lightmapParams.Shading.DiffuseWrap)
            .AddFloat("normal_offset", lightmapParams.Shading.NormalOffset)
            .AddFloat("luxel_size", lightmapParams.LuxelSize)
            .AddU32("max_atlas_size", lightmapParams.MaxAtlasSize)
            .AddFloat("cone_degrees", lightmapParams.ConeDegrees)
            .AddBool("ao_enabled", lightmapParams.Ao.Enabled);
        if (lightmapParams.Ao.Enabled)
            documentFingerprint.AddFloat("ao_max_distance", lightmapParams.Ao.MaxDistance)
                .AddU32("ao_ray_count", lightmapParams.Ao.RayCount);
    }
    documentFingerprint.AddU64("reachable_halo_count", reachableHalo.size());
    for (const ProbeHaloZone* zone : reachableHalo)
        documentFingerprint.AddU64("reachable_halo", zone->ContentHash);
    if (!probeVolumes.empty())
    {
        documentFingerprint.AddDependency("bounce_lights", HashDirectLights(bounceLights))
            .AddDependency("probe_volumes", HashProbeVolumes(probeVolumes))
            .AddFloat("probe_bounce_albedo", lightmapParams.Probe.BounceAlbedo);
        AddVec3(documentFingerprint, "probe_sky", lightmapParams.Probe.SkyColor);
        AddVec3(documentFingerprint, "probe_ground", lightmapParams.Probe.GroundColor);
        documentFingerprint.AddFloat("probe_max_ray_distance",
                                     lightmapParams.Probe.MaxRayDistance)
            .AddFloat("probe_classify_ray_distance",
                      lightmapParams.Probe.ClassifyRayDistance)
            .AddFloat("probe_classify_backface_ratio",
                      lightmapParams.Probe.ClassifyBackfaceRatio)
            .AddFloat("probe_diffuse_wrap", lightmapParams.Shading.DiffuseWrap)
            .AddFloat("probe_normal_offset", lightmapParams.Shading.NormalOffset)
            .AddU32("probe_ray_count", lightmapParams.ProbeRayCount);
    }
    fp.Document = documentFingerprint.Value();

    return fp;
}
