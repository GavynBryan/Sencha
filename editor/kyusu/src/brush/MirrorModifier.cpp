#include "MirrorModifier.h"

#include "FaceMaterial.h"

#include <algorithm>
#include <vector>

Plane ResolveMirrorPlane(const MirrorModifier& mirror, const Aabb3d& inputBounds)
{
    const Vec3d axis = LocalAxisVector(mirror.Axis);
    switch (mirror.Source)
    {
    case MirrorPlaneSource::Custom:
        return mirror.CustomPlane;
    case MirrorPlaneSource::BoundsCenter:
    {
        const Vec3d center = inputBounds.IsValid() ? inputBounds.Center() : Vec3d{};
        return Plane::FromNormalAndPoint(axis, center + axis * mirror.Offset);
    }
    case MirrorPlaneSource::Origin:
    default:
        // n.p + d = 0 with the plane at Offset along the axis: d = -Offset.
        return Plane::FromNormalAndDistance(axis, -mirror.Offset);
    }
}

Vec3d ReflectVector(Vec3d n, Vec3d v)
{
    return v - n * (2.0f * v.Dot(n));
}

Vec3d ReflectPoint(const Plane& plane, Vec3d p)
{
    return p - plane.Normal * (2.0f * plane.SignedDistanceTo(p));
}

BrushMesh MirrorBrushMesh(const BrushMesh& mesh, const Plane& localPlane)
{
    if (localPlane.Normal.SqrMagnitude() < 1e-12f)
        return mesh;
    const Plane plane = localPlane.Normalized();

    BrushMesh out = mesh;
    for (BrushVertex& vertex : out.Vertices)
        vertex.Position = ReflectPoint(plane, vertex.Position);

    std::vector<Vec3d> loopPositions;
    for (std::size_t i = 0; i < out.Faces.size(); ++i)
    {
        BrushFace& face = out.Faces[i];
        std::reverse(face.Loop.begin(), face.Loop.end());
        const Vec3d sourceNormal = BrushComputeFaceNormal(mesh, mesh.Faces[i]);
        face.Normal = BrushComputeFaceNormal(out, face);

        loopPositions.clear();
        loopPositions.reserve(face.Loop.size());
        for (const std::uint32_t index : face.Loop)
            if (index < out.Vertices.size())
                loopPositions.push_back(out.Vertices[index].Position);

        face.Material.Uv = MirrorFaceProjection(mesh.Faces[i].Material.Uv, sourceNormal,
                                                face.Normal, loopPositions);
    }
    return out;
}
