#pragma once

#include <cstdint>
#include <span>

#include <math/Mat.h>
#include <math/Vec.h>

struct MeshGeometry;
class BakeBvh;

// RGBM multiplier for the baked-direct vertex channel. Must match
// BAKED_DIRECT_RANGE in engine/shaders/mesh_frame.glsli (the shader decoder).
inline constexpr float kBakedDirectRange = 16.0f;

enum class BakeLightKind : std::uint8_t
{
    Point,
    Spot,
};

// A static light whose direct diffuse is baked into geometry vertices. World
// space. Spot fields are ignored for point lights. ConeScale/ConeOffset follow
// the runtime packing (RenderLight.h MakeSpotGpuLight) so the baked cone
// matches the shader cone exactly.
struct BakeDirectLight
{
    BakeLightKind Kind = BakeLightKind::Point;
    Vec3d Position;
    Vec3d Color = Vec3d(1.0f, 1.0f, 1.0f);
    float Intensity = 1.0f;
    float Range = 10.0f;

    Vec3d Direction = Vec3d(0.0f, 0.0f, -1.0f);
    float ConeScale = 0.0f;
    float ConeOffset = 1.0f;
};

struct DirectLightBakeParams
{
    // Match render.style.diffuse_wrap (DefaultRenderPipeline / the shader) so
    // the baked term equals the dynamic term for the same light.
    float DiffuseWrap = 0.25f;
    // Ray origin lift along the shading normal, to avoid self-occlusion.
    float NormalOffset = 0.02f;
};

// The baked static direct diffuse radiance at one world-space surface point
// (the same wrap-Lambert + windowed inverse-square model as the forward shader,
// with one occlusion ray per light). Shared by the vertex bake and the
// adaptive tessellator's error metric so both see identical values.
Vec3d EvaluateBakedDirectRadiance(const Vec3d& worldPosition,
                                  const Vec3d& worldNormal,
                                  std::span<const BakeDirectLight> lights,
                                  const BakeBvh& occluders,
                                  const DirectLightBakeParams& params);

// Compute each vertex's baked static direct diffuse and write it, RGBM-packed,
// into geometry.Vertices[].BakedDirect. Uses the same wrap-Lambert + windowed
// inverse-square model as the forward shader (lighting.glsli), traces one
// occlusion ray per (vertex, light) through `occluders` (which must already
// contain this geometry's world triangles plus any neighbor halo), and applies
// `worldTransform` to map local vertex positions/normals into the light/BVH
// world space. Deterministic. Returns true if any vertex received nonzero
// light (so the caller can set the baked-direct file flag).
bool BakeDirectLighting(MeshGeometry& geometry,
                        const Mat4& worldTransform,
                        std::span<const BakeDirectLight> lights,
                        const BakeBvh& occluders,
                        const DirectLightBakeParams& params);

// Pack a linear RGB radiance into the R8G8B8A8 RGBM the vertex channel stores.
// Zero radiance packs to zero (neutral). Exposed for tests.
std::uint32_t EncodeBakedDirectRgbm(const Vec3d& radiance);
