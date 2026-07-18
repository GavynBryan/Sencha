#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include <assets/static_mesh/MeshLoader.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <assets/static_mesh/StaticMeshFormat.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/StaticMeshPrimitives.h>
#include <render/static_mesh/MeshValidation.h>

namespace
{
    MeshGeometry MakeValidMesh()
    {
        return StaticMeshPrimitives::BuildCube(2.0f);
    }
}

TEST(StaticMeshValidation, CubeMeshValidates)
{
    const MeshValidationResult result = ValidateMeshGeometry(MakeValidMesh());
    EXPECT_TRUE(result.IsValid());
}

TEST(StaticMeshValidation, EmptyVertexBufferFails)
{
    MeshGeometry mesh = MakeValidMesh();
    mesh.Vertices.clear();

    const MeshValidationResult result = ValidateMeshGeometry(mesh);
    EXPECT_FALSE(result.IsValid());
}

TEST(StaticMeshValidation, OutOfRangeIndexFails)
{
    MeshGeometry mesh = MakeValidMesh();
    mesh.Indices[0] = static_cast<uint32_t>(mesh.Vertices.size());

    const MeshValidationResult result = ValidateMeshGeometry(mesh);
    EXPECT_FALSE(result.IsValid());
}

TEST(StaticMeshValidation, ComputeStaticMeshSectionBoundsUsesSectionIndices)
{
    MeshGeometry mesh;
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

    const Aabb3d bounds = ComputeMeshSectionBounds(mesh, mesh.Sections[0]);
    EXPECT_EQ(bounds.Min, Vec3d(0.0, 0.0, 0.0));
    EXPECT_EQ(bounds.Max, Vec3d(1.0, 1.0, 0.0));
}

TEST(StaticMeshValidation, SectionVertexRangeMustContainSectionIndices)
{
    MeshGeometry mesh;
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

    const MeshValidationResult result = ValidateMeshGeometry(mesh);
    EXPECT_FALSE(result.IsValid());
}

TEST(StaticMeshValidation, RecomputeStaticMeshBoundsUpdatesMeshAndSections)
{
    MeshGeometry mesh = MakeValidMesh();
    mesh.LocalBounds = Aabb3d::Empty();
    mesh.Sections[0].LocalBounds = Aabb3d::Empty();

    RecomputeMeshBounds(mesh);

    EXPECT_TRUE(mesh.LocalBounds.IsValid());
    EXPECT_TRUE(mesh.Sections[0].LocalBounds.IsValid());
    EXPECT_EQ(mesh.LocalBounds, mesh.Sections[0].LocalBounds);
}

TEST(StaticMeshSerialization, RoundTripPreservesCountsAndBounds)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    MeshLoader loader(logging);

    const MeshGeometry source = MakeValidMesh();

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(source, bytes));

    MeshGeometry loaded;
    ASSERT_TRUE(loader.LoadFromBytes(bytes, loaded));

    EXPECT_EQ(loaded.Vertices.size(), source.Vertices.size());
    EXPECT_EQ(loaded.Indices.size(), source.Indices.size());
    EXPECT_EQ(loaded.Sections.size(), source.Sections.size());
    EXPECT_EQ(loaded.LocalBounds, source.LocalBounds);
}

TEST(StaticMeshSerialization, BadMagicFails)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    MeshLoader loader(logging);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(MakeValidMesh(), bytes));
    bytes[0] = std::byte{'B'};

    MeshGeometry loaded;
    EXPECT_FALSE(loader.LoadFromBytes(bytes, loaded));
}

TEST(StaticMeshSerialization, WritesVersion4AndStride52)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(MakeValidMesh(), bytes));
    ASSERT_GE(bytes.size(), sizeof(SmeshFileHeader));

    SmeshFileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    EXPECT_EQ(header.Version, kSmeshFormatVersion);
    EXPECT_EQ(header.Version, 4u);
    EXPECT_EQ(header.VertexStride, sizeof(StaticMeshVertex));
    EXPECT_EQ(header.VertexStride, 52u);
}

TEST(StaticMeshSerialization, PreservesBakedDirectChannel)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    MeshLoader loader(logging);

    MeshGeometry source = MakeValidMesh();
    for (std::size_t i = 0; i < source.Vertices.size(); ++i)
        source.Vertices[i].BakedDirect = 0x11223344u + static_cast<std::uint32_t>(i);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(source, bytes));

    MeshGeometry loaded;
    ASSERT_TRUE(loader.LoadFromBytes(bytes, loaded));
    ASSERT_EQ(loaded.Vertices.size(), source.Vertices.size());
    for (std::size_t i = 0; i < source.Vertices.size(); ++i)
        EXPECT_EQ(loaded.Vertices[i].BakedDirect, source.Vertices[i].BakedDirect);
}

TEST(StaticMeshSerialization, RejectsPriorVersion)
{
    // One version is live at a time: a v3 file (before the baked-direct
    // channel) must fail rather than load with a mismatched vertex stride.
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    MeshLoader loader(logging);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(MakeValidMesh(), bytes));
    const std::uint32_t priorVersion = 3;
    std::memcpy(bytes.data() + offsetof(SmeshFileHeader, Version),
                &priorVersion, sizeof(priorVersion));

    MeshGeometry loaded;
    EXPECT_FALSE(loader.LoadFromBytes(bytes, loaded));
}
