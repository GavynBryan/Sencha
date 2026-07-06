#include "PreviewPrimitives.h"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    void ExpectWellFormed(const MeshGeometry& geometry)
    {
        ASSERT_FALSE(geometry.Vertices.empty());
        ASSERT_FALSE(geometry.Indices.empty());
        ASSERT_EQ(geometry.Indices.size() % 3, 0u);

        for (const uint32_t index : geometry.Indices)
            ASSERT_LT(index, geometry.Vertices.size());

        ASSERT_EQ(geometry.Sections.size(), 1u);
        EXPECT_EQ(geometry.Sections[0].MaterialSlot, 0u);
        EXPECT_EQ(geometry.Sections[0].IndexCount, geometry.Indices.size());
        EXPECT_EQ(geometry.Sections[0].VertexCount, geometry.Vertices.size());

        for (const StaticMeshVertex& vertex : geometry.Vertices)
        {
            // Unit normals and unit tangents with a +-1 handedness sign: what
            // the forward shader's TBN construction assumes.
            EXPECT_NEAR(vertex.Normal.Magnitude(), 1.0f, 1e-3f);
            const float tangentLength = std::sqrt(vertex.Tangent.X * vertex.Tangent.X
                                                  + vertex.Tangent.Y * vertex.Tangent.Y
                                                  + vertex.Tangent.Z * vertex.Tangent.Z);
            EXPECT_NEAR(tangentLength, 1.0f, 1e-3f);
            EXPECT_NEAR(std::abs(vertex.Tangent.W), 1.0f, 1e-6f);
        }
    }
}

TEST(PreviewPrimitives, SphereIsWellFormed)
{
    const MeshGeometry sphere = BuildPreviewPrimitive(PreviewPrimitive::Sphere);
    ExpectWellFormed(sphere);

    // Every point sits on the radius-0.5 shell and its normal points outward.
    for (const StaticMeshVertex& vertex : sphere.Vertices)
    {
        EXPECT_NEAR(vertex.Position.Magnitude(), 0.5f, 1e-3f);
        const Vec3d outward = vertex.Position.Normalized();
        EXPECT_NEAR(outward.Dot(vertex.Normal), 1.0f, 1e-3f);
    }
}

TEST(PreviewPrimitives, CubeIsWellFormed)
{
    const MeshGeometry cube = BuildPreviewPrimitive(PreviewPrimitive::Cube);
    ExpectWellFormed(cube);
    EXPECT_EQ(cube.Vertices.size(), 24u);
    EXPECT_EQ(cube.Indices.size(), 36u);
}

TEST(PreviewPrimitives, PlaneIsWellFormed)
{
    const MeshGeometry plane = BuildPreviewPrimitive(PreviewPrimitive::Plane);
    ExpectWellFormed(plane);
    EXPECT_EQ(plane.Vertices.size(), 4u);
    EXPECT_EQ(plane.Indices.size(), 6u);
}

TEST(PreviewPrimitives, TriangleWindingMatchesVertexNormals)
{
    // Every triangle's geometric normal must agree with its vertex normals;
    // an inverted quad renders inside-out (culling shows the far faces, whose
    // normals face away from the light, so lighting looks dead).
    for (const PreviewPrimitive kind :
         { PreviewPrimitive::Sphere, PreviewPrimitive::Cube, PreviewPrimitive::Plane })
    {
        const MeshGeometry geometry = BuildPreviewPrimitive(kind);
        for (std::size_t i = 0; i + 2 < geometry.Indices.size(); i += 3)
        {
            const StaticMeshVertex& a = geometry.Vertices[geometry.Indices[i]];
            const StaticMeshVertex& b = geometry.Vertices[geometry.Indices[i + 1]];
            const StaticMeshVertex& c = geometry.Vertices[geometry.Indices[i + 2]];

            const Vec3d geometric = (b.Position - a.Position).Cross(c.Position - a.Position);
            if (geometric.SqrMagnitude() <= 1e-8f)
                continue; // degenerate sliver at a sphere pole
            const Vec3d averaged = (a.Normal + b.Normal + c.Normal).Normalized();
            EXPECT_GT(geometric.Normalized().Dot(averaged), 0.0f)
                << PreviewPrimitiveName(kind) << " triangle " << i / 3 << " winds against its normals";
        }
    }
}
