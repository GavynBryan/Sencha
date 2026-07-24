#pragma once

#include <cstdint>
#include <span>

#include <math/Mat.h>
#include <math/Vec.h>

struct MeshGeometry;
class BakeBvh;

// Radiance ceiling for baked-direct lightmap texels, the RGB9E5 shared
// exponent format's representable maximum: (511/512) * 2^17.
inline constexpr float kBakedDirectMax = 65408.0f;

enum class BakeLightKind : std::uint8_t
{
    Point,
    Spot,
};

// A static light whose direct diffuse is baked into the zone lightmap. World
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

// The baked static direct diffuse radiance at one world-space surface point:
// the same wrap-Lambert + windowed inverse-square model as the forward shader
// (lighting.glsli), with one occlusion ray per light through `occluders`. The
// shared per-sample evaluator behind both the per-vertex bake and the lightmap
// luxel bake.
Vec3d EvaluateBakedDirectRadiance(const Vec3d& worldPosition,
                                  const Vec3d& worldNormal,
                                  std::span<const BakeDirectLight> lights,
                                  const BakeBvh& occluders,
                                  const DirectLightBakeParams& params);

// Pack a linear RGB radiance into the RGB9E5 shared-exponent texel a lightmap
// stores (VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 bit layout: 9-bit mantissas in
// r|g<<9|b<<18, 5-bit biased exponent in <<27). The sampler decodes texels
// before filtering, so bilinear results are linear in radiance. Zero radiance
// packs to zero. Exposed for tests.
std::uint32_t EncodeBakedDirectRgb9e5(const Vec3d& radiance);
