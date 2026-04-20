#include <render/MeshTypes.h>

namespace
{
    void AddFace(MeshData& mesh,
                 Vec3d a,
                 Vec3d b,
                 Vec3d c,
                 Vec3d d,
                 Vec3d normal)
    {
        const uint32_t base = static_cast<uint32_t>(mesh.Vertices.size());
        mesh.Vertices.push_back({ a, normal, Vec2d(0.0f, 0.0f) });
        mesh.Vertices.push_back({ b, normal, Vec2d(1.0f, 0.0f) });
        mesh.Vertices.push_back({ c, normal, Vec2d(1.0f, 1.0f) });
        mesh.Vertices.push_back({ d, normal, Vec2d(0.0f, 1.0f) });
        mesh.Indices.insert(mesh.Indices.end(), {
            base + 0, base + 1, base + 2,
            base + 0, base + 2, base + 3,
        });
    }
}

MeshData MeshPrimitives::BuildCube(float size)
{
    const float h = size * 0.5f;

    const Vec3d p000(-h, -h, -h);
    const Vec3d p001(-h, -h,  h);
    const Vec3d p010(-h,  h, -h);
    const Vec3d p011(-h,  h,  h);
    const Vec3d p100( h, -h, -h);
    const Vec3d p101( h, -h,  h);
    const Vec3d p110( h,  h, -h);
    const Vec3d p111( h,  h,  h);

    MeshData mesh;
    mesh.LocalBounds = Aabb3d::FromMinMax(Vec3d(-h, -h, -h), Vec3d(h, h, h));

    AddFace(mesh, p100, p000, p010, p110, Vec3d(0.0f, 0.0f, -1.0f));
    AddFace(mesh, p001, p101, p111, p011, Vec3d(0.0f, 0.0f, 1.0f));
    AddFace(mesh, p000, p001, p011, p010, Vec3d(-1.0f, 0.0f, 0.0f));
    AddFace(mesh, p101, p100, p110, p111, Vec3d(1.0f, 0.0f, 0.0f));
    AddFace(mesh, p010, p011, p111, p110, Vec3d(0.0f, 1.0f, 0.0f));
    AddFace(mesh, p000, p100, p101, p001, Vec3d(0.0f, -1.0f, 0.0f));

    mesh.Submeshes.push_back({
        .IndexOffset = 0,
        .IndexCount = static_cast<uint32_t>(mesh.Indices.size()),
        .MaterialSlot = 0,
    });
    return mesh;
}
