#include "PreviewPrimitives.h"

#include <cmath>
#include <numbers>

namespace
{
    constexpr float kPi = std::numbers::pi_v<float>;

    void FinishGeometry(MeshGeometry& geometry)
    {
        geometry.LocalBounds = Aabb3d::Empty();
        for (const StaticMeshVertex& vertex : geometry.Vertices)
            geometry.LocalBounds.ExpandToInclude(vertex.Position);

        StaticMeshSection section;
        section.IndexOffset = 0;
        section.IndexCount = static_cast<uint32_t>(geometry.Indices.size());
        section.VertexOffset = 0;
        section.VertexCount = static_cast<uint32_t>(geometry.Vertices.size());
        section.MaterialSlot = 0;
        section.LocalBounds = geometry.LocalBounds;
        geometry.Sections = { section };
    }

    MeshGeometry BuildSphere()
    {
        // UV sphere: enough segments that the silhouette reads smooth at panel
        // sizes; tangents follow +longitude so normal maps orient predictably.
        constexpr uint32_t kSegments = 48; // around the equator
        constexpr uint32_t kRings = 24;    // pole to pole
        constexpr float kRadius = 0.5f;

        MeshGeometry geometry;
        for (uint32_t ring = 0; ring <= kRings; ++ring)
        {
            const float v = static_cast<float>(ring) / static_cast<float>(kRings);
            const float phi = v * kPi; // 0 at +Y pole
            const float y = std::cos(phi);
            const float ringRadius = std::sin(phi);
            for (uint32_t segment = 0; segment <= kSegments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
                const float theta = u * 2.0f * kPi;
                const Vec3d normal(ringRadius * std::cos(theta), y, ringRadius * std::sin(theta));

                StaticMeshVertex vertex;
                vertex.Position = normal * kRadius;
                vertex.Normal = normal;
                vertex.Uv0 = Vec2d(u, v);
                vertex.Tangent = Vec4(-std::sin(theta), 0.0f, std::cos(theta), 1.0f);
                geometry.Vertices.push_back(vertex);
            }
        }

        const uint32_t stride = kSegments + 1;
        for (uint32_t ring = 0; ring < kRings; ++ring)
        {
            for (uint32_t segment = 0; segment < kSegments; ++segment)
            {
                const uint32_t a = ring * stride + segment;
                const uint32_t b = a + stride;
                // Outward CCW (the engine convention: geometric normal agrees
                // with the vertex normal; see the winding test).
                geometry.Indices.insert(geometry.Indices.end(), { a, a + 1, b, b, a + 1, b + 1 });
            }
        }

        FinishGeometry(geometry);
        return geometry;
    }

    void AddQuad(MeshGeometry& geometry,
                 const Vec3d& origin, const Vec3d& edgeU, const Vec3d& edgeV,
                 const Vec3d& normal)
    {
        const uint32_t base = static_cast<uint32_t>(geometry.Vertices.size());
        const Vec3d tangent = edgeU.Normalized();
        const Vec2d uvs[4] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
        const Vec3d corners[4] = { origin, origin + edgeU, origin + edgeU + edgeV, origin + edgeV };
        for (int i = 0; i < 4; ++i)
        {
            StaticMeshVertex vertex;
            vertex.Position = corners[i];
            vertex.Normal = normal;
            vertex.Uv0 = uvs[i];
            vertex.Tangent = Vec4(tangent.X, tangent.Y, tangent.Z, 1.0f);
            geometry.Vertices.push_back(vertex);
        }
        geometry.Indices.insert(geometry.Indices.end(),
                                { base, base + 1, base + 2, base, base + 2, base + 3 });
    }

    MeshGeometry BuildCube()
    {
        constexpr float h = 0.5f;
        MeshGeometry geometry;
        AddQuad(geometry, { -h, -h,  h }, { 2 * h, 0, 0 }, { 0, 2 * h, 0 }, { 0, 0, 1 });   // +Z
        AddQuad(geometry, {  h, -h, -h }, { -2 * h, 0, 0 }, { 0, 2 * h, 0 }, { 0, 0, -1 }); // -Z
        AddQuad(geometry, {  h, -h,  h }, { 0, 0, -2 * h }, { 0, 2 * h, 0 }, { 1, 0, 0 });  // +X
        AddQuad(geometry, { -h, -h, -h }, { 0, 0, 2 * h }, { 0, 2 * h, 0 }, { -1, 0, 0 });  // -X
        AddQuad(geometry, { -h,  h,  h }, { 2 * h, 0, 0 }, { 0, 0, -2 * h }, { 0, 1, 0 });  // +Y
        AddQuad(geometry, { -h, -h, -h }, { 2 * h, 0, 0 }, { 0, 0, 2 * h }, { 0, -1, 0 });  // -Y
        FinishGeometry(geometry);
        return geometry;
    }

    MeshGeometry BuildPlane()
    {
        MeshGeometry geometry;
        AddQuad(geometry, { -0.5f, -0.5f, 0.0f }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
        FinishGeometry(geometry);
        return geometry;
    }
}

const char* PreviewPrimitiveName(PreviewPrimitive kind)
{
    switch (kind)
    {
    case PreviewPrimitive::Sphere: return "Sphere";
    case PreviewPrimitive::Cube:   return "Cube";
    case PreviewPrimitive::Plane:  return "Plane";
    default:                       return "?";
    }
}

MeshGeometry BuildPreviewPrimitive(PreviewPrimitive kind)
{
    switch (kind)
    {
    case PreviewPrimitive::Cube:  return BuildCube();
    case PreviewPrimitive::Plane: return BuildPlane();
    case PreviewPrimitive::Sphere:
    default:                      return BuildSphere();
    }
}
