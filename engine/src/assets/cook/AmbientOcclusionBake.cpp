#include <assets/cook/AmbientOcclusionBake.h>

#include <assets/cook/BakeBvh.h>

float EvaluateAmbientOcclusion(const Vec3d& position, const Vec3d& normal,
                               const BakeBvh& occluders,
                               const AmbientOcclusionBakeParams& params)
{
    const Vec3d origin = position + normal * params.NormalOffset;
    float open = 0.0f;
    float total = 0.0f;
    for (const Vec3d& direction : params.RayTable)
    {
        const float cosine = normal.Dot(direction);
        if (cosine <= 1e-4f)
            continue;
        total += cosine;
        if (!occluders.SegmentOccluded(origin,
                                       origin + direction * params.MaxDistance))
            open += cosine;
    }
    return total > 0.0f ? open / total : 1.0f;
}
