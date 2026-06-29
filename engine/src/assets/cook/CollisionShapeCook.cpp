#include <assets/cook/CollisionShapeCook.h>

#include "../../physics/JoltStartup.h"

#include <cstring>
#include <sstream>

#include <Jolt/Jolt.h>

#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Math/Float3.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

std::vector<std::byte> BakeCollisionBlob(
    std::span<const Vec3d> positions,
    std::span<const uint32_t> indices,
    std::string* error)
{
    EnsureJoltRegistered(); // Jolt allocation needs its allocator installed

    if (indices.size() < 3)
    {
        if (error != nullptr)
            *error = "collision cook: fewer than one triangle";
        return {};
    }

    JPH::VertexList vertices;
    vertices.reserve(positions.size());
    for (const Vec3d& p : positions)
        vertices.push_back(JPH::Float3(p.X, p.Y, p.Z));

    JPH::IndexedTriangleList triangles;
    triangles.reserve(indices.size() / 3);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2], 0));

    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    settings.Sanitize(); // drop degenerate triangles

    JPH::ShapeSettings::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        if (error != nullptr)
            *error = std::string("collision cook: ") + result.GetError().c_str();
        return {};
    }

    std::ostringstream stream(std::ios::binary);
    JPH::StreamOutWrapper out(stream);
    result.Get()->SaveBinaryState(out);

    const std::string serialized = stream.str();
    std::vector<std::byte> blob(serialized.size());
    std::memcpy(blob.data(), serialized.data(), serialized.size());
    return blob;
}
