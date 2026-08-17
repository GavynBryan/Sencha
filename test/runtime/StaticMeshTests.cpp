#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include <assets/static_mesh/MeshLoader.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <assets/static_mesh/StaticMeshFormat.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/StaticMeshPrimitives.h>
#include <render/static_mesh/MeshGeometry.h>
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

namespace
{
    // One triangle per section, all over the same three vertices: the section
    // count is what is under test, not the geometry.
    MeshGeometry MakeMeshWithSections(std::size_t sectionCount)
    {
        MeshGeometry mesh;
        const Vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
        mesh.Vertices = {
            { Vec3d(0.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(0.0, 0.0), tangent },
            { Vec3d(1.0, 0.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(1.0, 0.0), tangent },
            { Vec3d(0.0, 1.0, 0.0), Vec3d(0.0, 1.0, 0.0), Vec2d(0.0, 1.0), tangent },
        };
        for (std::size_t index = 0; index < sectionCount; ++index)
        {
            mesh.Indices.insert(mesh.Indices.end(), { 0u, 1u, 2u });
            mesh.Sections.push_back({
                .IndexOffset = static_cast<uint32_t>(index * 3),
                .IndexCount = 3,
                .VertexOffset = 0,
                .VertexCount = 3,
                .MaterialSlot = 0,
            });
        }
        RecomputeMeshBounds(mesh);
        return mesh;
    }
}

TEST(StaticMeshValidation, SectionCountIsCappedAtTheSectionMaskWidth)
{
    // Extraction tests section membership with `1u << sectionIndex` against a
    // 32-bit mask, so a 33rd section shifts past the mask's width. Rejecting
    // it here is what keeps that shift defined.
    EXPECT_TRUE(ValidateMeshGeometry(MakeMeshWithSections(31)).IsValid());
    EXPECT_TRUE(ValidateMeshGeometry(MakeMeshWithSections(kMaxMeshSections)).IsValid());

    const MeshValidationResult tooMany =
        ValidateMeshGeometry(MakeMeshWithSections(kMaxMeshSections + 1));
    ASSERT_FALSE(tooMany.IsValid());
    EXPECT_NE(tooMany.Errors.front().Message.find("section mask limit"),
              std::string::npos);
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

TEST(StaticMeshSerialization, WritesVersion5AndStride52)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(MakeValidMesh(), bytes));
    ASSERT_GE(bytes.size(), sizeof(SmeshFileHeader));

    SmeshFileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    EXPECT_EQ(header.Version, kSmeshFormatVersion);
    EXPECT_EQ(header.Version, 5u);
    EXPECT_EQ(header.VertexStride, sizeof(StaticMeshVertex));
    EXPECT_EQ(header.VertexStride, 52u);
}

TEST(StaticMeshSerialization, PreservesLightmapUvChannel)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    MeshLoader loader(logging);

    MeshGeometry source = MakeValidMesh();
    for (std::size_t i = 0; i < source.Vertices.size(); ++i)
    {
        source.Vertices[i].LightmapU = static_cast<std::uint16_t>(0x1122u + i);
        source.Vertices[i].LightmapV = static_cast<std::uint16_t>(0x3344u + i);
    }

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(source, bytes));
    SmeshFileHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    EXPECT_NE(header.Flags & kSmeshFlagLightmapUv, 0u);

    MeshGeometry loaded;
    ASSERT_TRUE(loader.LoadFromBytes(bytes, loaded));
    ASSERT_EQ(loaded.Vertices.size(), source.Vertices.size());
    for (std::size_t i = 0; i < source.Vertices.size(); ++i)
    {
        EXPECT_EQ(loaded.Vertices[i].LightmapU, source.Vertices[i].LightmapU);
        EXPECT_EQ(loaded.Vertices[i].LightmapV, source.Vertices[i].LightmapV);
    }
}

TEST(StaticMeshSerialization, RejectsPriorVersion)
{
    // One version is live at a time: a v4 file (the baked-direct vertex
    // channel that the lightmap UVs replaced) must fail rather than load
    // with reinterpreted channel bytes.
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    MeshLoader loader(logging);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteToBytes(MakeValidMesh(), bytes));
    const std::uint32_t priorVersion = 4;
    std::memcpy(bytes.data() + offsetof(SmeshFileHeader, Version),
                &priorVersion, sizeof(priorVersion));

    MeshGeometry loaded;
    EXPECT_FALSE(loader.LoadFromBytes(bytes, loaded));
}

// --- lightmap-UV presence: the baked-atlas participation gate ---
//
// Extraction reads GpuStaticMesh::HasLightmapUvs to decide whether an instance
// may sample its zone's baked atlas. That field is filled during GPU upload,
// which needs a device, so the rule it is filled from lives here where it runs
// headlessly. Getting it wrong is silent: a mesh wrongly marked as carrying UVs
// samples the atlas at texel (0,0) instead of skipping the fetch.

TEST(GeometryHasLightmapUvs, IsFalseForGeometryThatWasNeverUnwrapped)
{
    MeshGeometry geometry = MakeValidMesh();
    for (const StaticMeshVertex& vertex : geometry.Vertices)
    {
        ASSERT_EQ(vertex.LightmapU, 0);
        ASSERT_EQ(vertex.LightmapV, 0);
    }
    EXPECT_FALSE(GeometryHasLightmapUvs(geometry));
}

TEST(GeometryHasLightmapUvs, IsFalseForEmptyGeometry)
{
    EXPECT_FALSE(GeometryHasLightmapUvs(MeshGeometry{}));
}

TEST(GeometryHasLightmapUvs, IsTrueWhenAnyVertexCarriesAUv)
{
    MeshGeometry geometry = MakeValidMesh();
    ASSERT_GE(geometry.Vertices.size(), 3u);
    // The last vertex, so a scan that stops early still has to reach it.
    geometry.Vertices.back().LightmapV = 1;
    EXPECT_TRUE(GeometryHasLightmapUvs(geometry));
}

TEST(GeometryHasLightmapUvs, ChecksBothAxes)
{
    MeshGeometry onlyU = MakeValidMesh();
    onlyU.Vertices[0].LightmapU = 7;
    EXPECT_TRUE(GeometryHasLightmapUvs(onlyU));

    MeshGeometry onlyV = MakeValidMesh();
    onlyV.Vertices[0].LightmapV = 7;
    EXPECT_TRUE(GeometryHasLightmapUvs(onlyV));
}
