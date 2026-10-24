#include "brush/BrushBoxFit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

namespace
{
// Faces closer to parallel than this, in the cosine of their angle, are a pair.
constexpr float kParallel = 0.9999f;
constexpr float kPerpendicular = 1.0e-3f;

Vec3d LoopNormal(std::span<const Vec3d> positions, const BrushFace& face)
{
    Vec3d normal = Vec3d::Zero();
    for (std::size_t i = 0; i < face.Loop.size(); ++i)
    {
        const Vec3d& a = positions[face.Loop[i]];
        const Vec3d& b = positions[face.Loop[(i + 1) % face.Loop.size()]];
        normal.X += (a.Y - b.Y) * (a.Z + b.Z);
        normal.Y += (a.Z - b.Z) * (a.X + b.X);
        normal.Z += (a.X - b.X) * (a.Y + b.Y);
    }
    const float length = normal.Magnitude();
    return length > 0.0f ? normal * (1.0f / length) : Vec3d::Zero();
}
} // namespace

std::optional<OrientedBox> FitOrientedBox(const BrushMesh& mesh, std::span<const Vec3d> positions, std::string& reason)
{
    if (mesh.Faces.size() != 6 || mesh.Vertices.size() != 8 || positions.size() != mesh.Vertices.size())
    {
        reason = std::format("it has {} faces and {} corners; a box has 6 and 8", mesh.Faces.size(),
                             mesh.Vertices.size());
        return std::nullopt;
    }
    std::array<Vec3d, 6> normals{};
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (mesh.Faces[i].Loop.size() != 4)
        {
            reason = "a face is not four-sided";
            return std::nullopt;
        }
        normals[i] = LoopNormal(positions, mesh.Faces[i]);
        if (normals[i] == Vec3d::Zero())
        {
            reason = "a face has no area";
            return std::nullopt;
        }
    }

    std::array<Vec3d, 3> axes{};
    std::size_t found = 0;
    std::array<bool, 6> paired{};
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (paired[i])
            continue;
        for (std::size_t j = i + 1; j < 6 && !paired[i]; ++j)
            if (!paired[j] && normals[i].Dot(normals[j]) < -kParallel)
            {
                paired[i] = paired[j] = true;
                if (found < 3)
                    axes[found] = normals[i];
                ++found;
            }
    }
    if (found != 3 || std::ranges::find(paired, false) != paired.end())
    {
        reason = "its faces are not three pairs of opposite, parallel sides";
        return std::nullopt;
    }
    for (std::size_t i = 0; i < 3; ++i)
        if (std::abs(axes[i].Dot(axes[(i + 1) % 3])) > kPerpendicular)
        {
            reason = "its sides are not square to each other";
            return std::nullopt;
        }
    // Each axis the one nearest the frame's own, pointing its way, so a box
    // square to its frame comes out unturned.
    const std::array<Vec3d, 3> frame{ Vec3d(1.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f), Vec3d(0.0f, 0.0f, 1.0f) };
    for (std::size_t target = 0; target < 3; ++target)
    {
        std::size_t best = target;
        for (std::size_t candidate = target + 1; candidate < 3; ++candidate)
            if (std::abs(axes[candidate].Dot(frame[target])) > std::abs(axes[best].Dot(frame[target])))
                best = candidate;
        std::swap(axes[target], axes[best]);
        if (axes[target].Dot(frame[target]) < 0.0f)
            axes[target] = axes[target] * -1.0f;
    }
    // Right-handed, so the three axes are a rotation.
    if (axes[0].Cross(axes[1]).Dot(axes[2]) < 0.0f)
        axes[2] = axes[2] * -1.0f;

    std::array<float, 3> low{};
    std::array<float, 3> high{};
    for (std::size_t a = 0; a < 3; ++a)
    {
        low[a] = high[a] = positions[0].Dot(axes[a]);
        for (const Vec3d& position : positions)
        {
            low[a] = std::min(low[a], position.Dot(axes[a]));
            high[a] = std::max(high[a], position.Dot(axes[a]));
        }
    }
    const float size = std::max({ high[0] - low[0], high[1] - low[1], high[2] - low[2] });
    const float tolerance = std::max(1.0e-4f, size * 1.0e-4f);
    for (const Vec3d& position : positions)
        for (std::size_t a = 0; a < 3; ++a)
        {
            const float along = position.Dot(axes[a]);
            if (std::abs(along - low[a]) > tolerance && std::abs(along - high[a]) > tolerance)
            {
                reason = "a corner is not on the box its sides make";
                return std::nullopt;
            }
        }
    for (std::size_t a = 0; a < 3; ++a)
        if (high[a] - low[a] <= tolerance)
        {
            reason = "it is flat";
            return std::nullopt;
        }

    OrientedBox box;
    box.Orientation = Quatf::FromBasis(axes[0], axes[1], axes[2]).Normalized();
    box.HalfExtents = Vec3d((high[0] - low[0]) * 0.5f, (high[1] - low[1]) * 0.5f, (high[2] - low[2]) * 0.5f);
    box.Center = axes[0] * ((low[0] + high[0]) * 0.5f) + axes[1] * ((low[1] + high[1]) * 0.5f)
        + axes[2] * ((low[2] + high[2]) * 0.5f);
    return box;
}
