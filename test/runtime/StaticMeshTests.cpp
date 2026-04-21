#include <gtest/gtest.h>

#include <assets/static_mesh/StaticMeshLoader.h>
#include <assets/static_mesh/StaticMeshSerializer.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/StaticMeshPrimitives.h>
#include <render/static_mesh/StaticMeshValidation.h>

namespace
{
    StaticMeshData MakeValidMesh()
    {
        return StaticMeshPrimitives::BuildCube(2.0f);
    }
}

TEST(StaticMeshValidation, CubeMeshValidates)
{
    const StaticMeshValidationResult result = ValidateStaticMeshData(MakeValidMesh());
    EXPECT_TRUE(result.IsValid());
}

TEST(StaticMeshValidation, EmptyVertexBufferFails)
{
    StaticMeshData mesh = MakeValidMesh();
    mesh.Vertices.clear();

    const StaticMeshValidationResult result = ValidateStaticMeshData(mesh);
    EXPECT_FALSE(result.IsValid());
}

TEST(StaticMeshValidation, OutOfRangeIndexFails)
{
    StaticMeshData mesh = MakeValidMesh();
    mesh.Indices[0] = static_cast<uint32_t>(mesh.Vertices.size());

    const StaticMeshValidationResult result = ValidateStaticMeshData(mesh);
    EXPECT_FALSE(result.IsValid());
}

TEST(StaticMeshValidation, ComputeStaticMeshSectionBoundsUsesSectionIndices)
{
    StaticMeshData mesh;
    mesh.Vertices = {
        { Vec3d(0.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(0.0, 0.0) },
        { Vec3d(1.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(1.0, 0.0) },
        { Vec3d(0.0, 1.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(0.0, 1.0) },
        { Vec3d(10.0, 10.0, 10.0), Vec3d(0.0, 1.0, 0.0), Vec2d(1.0, 1.0) },
    };
    mesh.Indices = { 0, 1, 2 };
    mesh.Sections.push_back({
        .IndexOffset = 0,
        .IndexCount = 3,
        .VertexOffset = 0,
        .VertexCount = 3,
        .MaterialSlot = 0,
    });

    const Aabb3d bounds = ComputeStaticMeshSectionBounds(mesh, mesh.Sections[0]);
    EXPECT_EQ(bounds.Min, Vec3d(0.0, 0.0, 0.0));
    EXPECT_EQ(bounds.Max, Vec3d(1.0, 1.0, 0.0));
}

TEST(StaticMeshValidation, SectionVertexRangeMustContainSectionIndices)
{
    StaticMeshData mesh;
    mesh.Vertices = {
        { Vec3d(0.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(0.0, 0.0) },
        { Vec3d(1.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(1.0, 0.0) },
        { Vec3d(0.0, 1.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(0.0, 1.0) },
    };
    mesh.Indices = { 0, 1, 2 };
    mesh.Sections.push_back({
        .IndexOffset = 0,
        .IndexCount = 3,
        .VertexOffset = 1,
        .VertexCount = 1,
        .MaterialSlot = 0,
    });

    const StaticMeshValidationResult result = ValidateStaticMeshData(mesh);
    EXPECT_FALSE(result.IsValid());
}

TEST(StaticMeshValidation, RecomputeStaticMeshBoundsUpdatesMeshAndSections)
{
    StaticMeshData mesh = MakeValidMesh();
    mesh.LocalBounds = Aabb3d::Empty();
    mesh.Sections[0].LocalBounds = Aabb3d::Empty();

    RecomputeStaticMeshBounds(mesh);

    EXPECT_TRUE(mesh.LocalBounds.IsValid());
    EXPECT_TRUE(mesh.Sections[0].LocalBounds.IsValid());
    EXPECT_EQ(mesh.LocalBounds, mesh.Sections[0].LocalBounds);
}

TEST(StaticMeshSerialization, RoundTripPreservesCountsAndBounds)
{
    LoggingProvider logging;
    StaticMeshSerializer serializer(logging);
    StaticMeshLoader loader(logging);

    const StaticMeshData source = MakeValidMesh();

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(source, bytes));

    StaticMeshData loaded;
    ASSERT_TRUE(loader.LoadFromBytes(bytes, loaded));

    EXPECT_EQ(loaded.Vertices.size(), source.Vertices.size());
    EXPECT_EQ(loaded.Indices.size(), source.Indices.size());
    EXPECT_EQ(loaded.Sections.size(), source.Sections.size());
    EXPECT_EQ(loaded.LocalBounds, source.LocalBounds);
}

TEST(StaticMeshSerialization, BadMagicFails)
{
    LoggingProvider logging;
    StaticMeshSerializer serializer(logging);
    StaticMeshLoader loader(logging);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(MakeValidMesh(), bytes));
    bytes[0] = std::byte{'B'};

    StaticMeshData loaded;
    EXPECT_FALSE(loader.LoadFromBytes(bytes, loaded));
}
