#include "BrushBox.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

std::optional<BrushBox> ExtractBrushBox(
    const BrushMesh& mesh, const Transform3f& transform, std::string& error)
{
    return ExtractBrushBox(mesh, transform.ToMat4(), error);
}

std::optional<BrushBox> ExtractBrushBox(
    const BrushMesh& mesh, const Mat4& transform, std::string& error)
{
    const auto fail = [&error](const char* message) -> std::optional<BrushBox>
    {
        error = message;
        return std::nullopt;
    };
    if (mesh.Vertices.size() != 8 || mesh.Faces.size() != 6)
        return fail("Sensor geometry must be a closed box with eight vertices and six quad faces.");
    for (const BrushFace& face : mesh.Faces)
    {
        if (face.Loop.size() != 4)
            return fail("Sensor box faces must be quads; rebuild the volume as a box.");
        for (const std::uint32_t vertex : face.Loop)
            if (vertex >= mesh.Vertices.size())
                return fail("Sensor box has an invalid face vertex index.");
    }

    std::array<Vec3d, 8> points;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        points[index] = transform.TransformPoint(mesh.Vertices[index].Position);
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(points[index][axis]))
                return fail("Sensor box contains a non-finite position or transform.");
    }

    const auto& first = mesh.Faces.front().Loop;
    const Vec3d edgeX = points[first[1]] - points[first[0]];
    const Vec3d edgeY = points[first[3]] - points[first[0]];
    const float lengthX = edgeX.Magnitude();
    const float lengthY = edgeY.Magnitude();
    if (lengthX <= 1e-5f || lengthY <= 1e-5f)
        return fail("Sensor box dimensions must be greater than 0.00001 metres.");
    const Vec3d x = edgeX / lengthX;
    const Vec3d y = edgeY / lengthY;
    if (std::abs(x.Dot(y)) > 1e-5f)
        return fail("Sensor box is sheared; its adjacent edges must be perpendicular.");
    const Vec3d z = x.Cross(y).Normalized();
    const std::array<Vec3d, 3> axes{ x, z.Cross(x).Normalized(), z };

    Vec3d low = Vec3d::Zero();
    Vec3d high = Vec3d::Zero();
    std::array<Vec3d, 8> projected;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const Vec3d relative = points[index] - points[first[0]];
        for (int axis = 0; axis < 3; ++axis)
        {
            projected[index][axis] = relative.Dot(axes[axis]);
            low[axis] = std::min(low[axis], projected[index][axis]);
            high[axis] = std::max(high[axis], projected[index][axis]);
        }
    }

    const Vec3d dimensions = high - low;
    for (int axis = 0; axis < 3; ++axis)
        if (dimensions[axis] <= 1e-5f)
            return fail("Sensor box dimensions must be greater than 0.00001 metres.");
    std::array<unsigned, 8> corners{};
    unsigned usedCorners = 0;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        unsigned corner = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float tolerance = dimensions[axis] * 1e-5f;
            if (std::abs(projected[index][axis] - low[axis]) <= tolerance)
                continue;
            if (std::abs(projected[index][axis] - high[axis]) > tolerance)
                return fail("Sensor volume is not an oriented box; a bounding-box approximation is unsupported.");
            corner |= 1u << axis;
        }
        if ((usedCorners & (1u << corner)) != 0)
            return fail("Sensor box has coincident or missing corners.");
        corners[index] = corner;
        usedCorners |= 1u << corner;
    }

    unsigned usedFaces = 0;
    for (const BrushFace& face : mesh.Faces)
    {
        unsigned commonHigh = 7;
        unsigned commonLow = 7;
        unsigned faceCorners = 0;
        for (std::size_t index = 0; index < face.Loop.size(); ++index)
        {
            const unsigned corner = corners[face.Loop[index]];
            const unsigned next = corners[face.Loop[(index + 1) % face.Loop.size()]];
            if (std::popcount(corner ^ next) != 1 || (faceCorners & (1u << corner)) != 0)
                return fail("Sensor box faces must connect four distinct adjacent corners.");
            faceCorners |= 1u << corner;
            commonHigh &= corner;
            commonLow &= ~corner;
        }
        const unsigned plane = commonHigh | commonLow;
        if (std::popcount(plane) != 1)
            return fail("Sensor box has a non-planar or invalid face.");
        const unsigned faceIndex = std::countr_zero(plane) * 2u + (commonHigh != 0 ? 1u : 0u);
        if ((usedFaces & (1u << faceIndex)) != 0)
            return fail("Sensor box is not closed: a face is duplicated and another is missing.");
        usedFaces |= 1u << faceIndex;
    }

    const Vec3d middle = (low + high) * 0.5f;
    error.clear();
    return BrushBox{
        points[first[0]] + axes[0] * middle.X + axes[1] * middle.Y + axes[2] * middle.Z,
        Quatf::FromBasis(axes[0], axes[1], axes[2]).Normalized(), dimensions * 0.5f,
    };
}
