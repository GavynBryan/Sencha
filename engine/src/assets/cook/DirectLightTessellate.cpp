#include <assets/cook/DirectLightTessellate.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include <assets/cook/BakeBvh.h>
#include <assets/cook/DirectLightBake.h>
#include <render/static_mesh/MeshGeometry.h>
#include <render/static_mesh/MeshValidation.h>

namespace
{
    constexpr std::uint32_t kNoMidpoint = 0xFFFFFFFFu;

    std::uint64_t EdgeKey(std::uint32_t a, std::uint32_t b)
    {
        const std::uint32_t lo = a < b ? a : b;
        const std::uint32_t hi = a < b ? b : a;
        return (static_cast<std::uint64_t>(lo) << 32) | hi;
    }

    float Quantize(float value, float quantum)
    {
        return std::round(value / quantum) * quantum;
    }

    float MaxChannelDiff(const Vec3d& a, const Vec3d& b)
    {
        return std::max({ std::abs(a.X - b.X), std::abs(a.Y - b.Y),
                          std::abs(a.Z - b.Z) });
    }
}

std::size_t TessellateForDirectBake(MeshGeometry& geometry,
                                    const Mat4& worldTransform,
                                    std::span<const BakeDirectLight> lights,
                                    const BakeBvh& occluders,
                                    const DirectLightBakeParams& bakeParams,
                                    const DirectTessellationParams& tessParams)
{
    if (lights.empty() || geometry.Indices.empty() || geometry.Sections.empty())
        return 0;

    const std::size_t baseVertexCount = geometry.Vertices.size();
    const std::size_t vertexCap = static_cast<std::size_t>(
        static_cast<double>(baseVertexCount) * tessParams.MaxVertexGrowth);

    auto worldPosOf = [&](std::uint32_t index) {
        return worldTransform.TransformPoint(geometry.Vertices[index].Position);
    };
    auto worldNormalOf = [&](std::uint32_t index) {
        return worldTransform.TransformVector(geometry.Vertices[index].Normal)
            .Normalized();
    };

    // Per-vertex baked radiance, extended as midpoints are inserted.
    std::vector<Vec3d> radiance(geometry.Vertices.size());
    for (std::size_t i = 0; i < geometry.Vertices.size(); ++i)
        radiance[i] = EvaluateBakedDirectRadiance(
            worldPosOf(static_cast<std::uint32_t>(i)),
            worldNormalOf(static_cast<std::uint32_t>(i)), lights, occluders, bakeParams);

    // Memoized split decision for one edge (one refinement level). Pure in the
    // edge endpoints, so an edge shared within a section is decided once and
    // both triangles get the same conforming midpoint (no T-junction).
    auto considerEdge = [&](std::unordered_map<std::uint64_t, std::uint32_t>& memo,
                            std::uint32_t a, std::uint32_t b) -> std::uint32_t {
        const std::uint64_t key = EdgeKey(a, b);
        const auto found = memo.find(key);
        if (found != memo.end())
            return found->second;

        std::uint32_t result = kNoMidpoint;
        const float edgeLength = (worldPosOf(b) - worldPosOf(a)).Magnitude();
        if (edgeLength >= tessParams.MinEdgeLength
            && geometry.Vertices.size() < vertexCap)
        {
            const StaticMeshVertex& va = geometry.Vertices[a];
            const StaticMeshVertex& vb = geometry.Vertices[b];
            Vec3d midLocal = (va.Position + vb.Position) * 0.5f;
            midLocal = Vec3d(Quantize(midLocal.X, tessParams.PositionQuantum),
                             Quantize(midLocal.Y, tessParams.PositionQuantum),
                             Quantize(midLocal.Z, tessParams.PositionQuantum));
            const Vec3d midNormalLocal = (va.Normal + vb.Normal).Normalized();
            const Vec3d midWorld = worldTransform.TransformPoint(midLocal);
            const Vec3d midWorldNormal =
                worldTransform.TransformVector(midNormalLocal).Normalized();

            bool nearLight = false;
            for (const BakeDirectLight& light : lights)
                if ((light.Position - midWorld).Magnitude()
                    < light.Range + tessParams.LightProximityMargin)
                {
                    nearLight = true;
                    break;
                }

            if (nearLight)
            {
                const Vec3d midRadiance = EvaluateBakedDirectRadiance(
                    midWorld, midWorldNormal, lights, occluders, bakeParams);
                const Vec3d interpolated = (radiance[a] + radiance[b]) * 0.5f;
                if (MaxChannelDiff(midRadiance, interpolated) > tessParams.ErrorTolerance)
                {
                    StaticMeshVertex mid{};
                    mid.Position = midLocal;
                    mid.Normal = midNormalLocal;
                    mid.Uv0 = (va.Uv0 + vb.Uv0) * 0.5f;
                    mid.Tangent = va.Tangent;
                    result = static_cast<std::uint32_t>(geometry.Vertices.size());
                    geometry.Vertices.push_back(mid);
                    radiance.push_back(midRadiance);
                }
            }
        }

        memo.emplace(key, result);
        return result;
    };

    // Refine one section's triangle list in place, to the depth cap.
    auto refineSection = [&](std::vector<std::uint32_t>& indices) {
        for (int depth = 0; depth < tessParams.MaxDepth; ++depth)
        {
            std::unordered_map<std::uint64_t, std::uint32_t> memo;
            std::vector<std::uint32_t> next;
            next.reserve(indices.size());
            bool splitAny = false;

            for (std::size_t t = 0; t + 2 < indices.size(); t += 3)
            {
                const std::uint32_t a = indices[t];
                const std::uint32_t b = indices[t + 1];
                const std::uint32_t c = indices[t + 2];
                const std::uint32_t mAB = considerEdge(memo, a, b);
                const std::uint32_t mBC = considerEdge(memo, b, c);
                const std::uint32_t mCA = considerEdge(memo, c, a);

                auto emit = [&](std::uint32_t i0, std::uint32_t i1, std::uint32_t i2) {
                    next.push_back(i0);
                    next.push_back(i1);
                    next.push_back(i2);
                };

                const bool sAB = mAB != kNoMidpoint;
                const bool sBC = mBC != kNoMidpoint;
                const bool sCA = mCA != kNoMidpoint;
                const int splitCount = int(sAB) + int(sBC) + int(sCA);
                splitAny = splitAny || splitCount > 0;

                if (splitCount == 0)
                    emit(a, b, c);
                else if (splitCount == 3)
                {
                    emit(a, mAB, mCA);
                    emit(mAB, b, mBC);
                    emit(mCA, mBC, c);
                    emit(mAB, mBC, mCA);
                }
                else if (splitCount == 1)
                {
                    if (sAB) { emit(a, mAB, c); emit(mAB, b, c); }
                    else if (sBC) { emit(a, b, mBC); emit(a, mBC, c); }
                    else { emit(a, b, mCA); emit(b, c, mCA); }
                }
                else // splitCount == 2
                {
                    if (!sCA) { emit(mAB, b, mBC); emit(a, mAB, mBC); emit(a, mBC, c); }
                    else if (!sAB) { emit(mBC, c, mCA); emit(a, b, mBC); emit(a, mBC, mCA); }
                    else { emit(a, mAB, mCA); emit(mAB, b, c); emit(mAB, c, mCA); }
                }
            }

            indices = std::move(next);
            if (!splitAny)
                break;
        }
    };

    // Refine each section over its own index range, then rebuild the index
    // buffer and section table (indices are global; the draw base is zero).
    std::vector<std::uint32_t> newIndices;
    std::vector<StaticMeshSection> newSections;
    newSections.reserve(geometry.Sections.size());
    for (const StaticMeshSection& section : geometry.Sections)
    {
        std::vector<std::uint32_t> sectionIndices(
            geometry.Indices.begin() + section.IndexOffset,
            geometry.Indices.begin() + section.IndexOffset + section.IndexCount);
        refineSection(sectionIndices);

        StaticMeshSection updated = section;
        updated.IndexOffset = static_cast<std::uint32_t>(newIndices.size());
        updated.IndexCount = static_cast<std::uint32_t>(sectionIndices.size());

        std::uint32_t minVertex = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t maxVertex = 0;
        Aabb3d bounds = Aabb3d::Empty();
        for (std::uint32_t index : sectionIndices)
        {
            minVertex = std::min(minVertex, index);
            maxVertex = std::max(maxVertex, index);
            bounds.ExpandToInclude(geometry.Vertices[index].Position);
        }
        updated.VertexOffset = sectionIndices.empty() ? 0 : minVertex;
        updated.VertexCount = sectionIndices.empty() ? 0 : (maxVertex - minVertex + 1);
        updated.LocalBounds = bounds;

        newIndices.insert(newIndices.end(), sectionIndices.begin(), sectionIndices.end());
        newSections.push_back(updated);
    }

    geometry.Indices = std::move(newIndices);
    geometry.Sections = std::move(newSections);
    RecomputeMeshBounds(geometry);
    return geometry.Vertices.size() - baseVertexCount;
}
