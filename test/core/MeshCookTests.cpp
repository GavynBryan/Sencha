// Stage 4c (docs/assets/pipeline.md, Decisions B, M): the glTF mesh cook.
// Zero-thread, no filesystem for the glTF path — fixtures are built in
// memory (data-URI .gltf and hand-assembled .glb) and importers write to a
// memory output. The Blender front end is the one exception: it shells out
// by design, so its test runs against the real executable and skips when
// Blender is not installed.

#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/cook/BlendCook.h>
#include <assets/cook/MeshCook.h>
#include <assets/static_mesh/MeshLoader.h>
#include <core/logging/LoggingProvider.h>
#include <assets/skinned_mesh/SkinnedMeshData.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace
{
    class MemoryCookOutputWriter final : public ICookOutputWriter
    {
    public:
        bool WriteBytes(std::string_view fileRelPath, std::span<const std::byte> bytes) override
        {
            Files[std::string(fileRelPath)].assign(bytes.begin(), bytes.end());
            return true;
        }

        std::map<std::string, std::vector<std::byte>> Files;
    };

    std::string Base64Encode(std::span<const std::byte> bytes)
    {
        static constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            uint32_t chunk = static_cast<uint32_t>(bytes[i]) << 16;
            if (i + 1 < bytes.size())
                chunk |= static_cast<uint32_t>(bytes[i + 1]) << 8;
            if (i + 2 < bytes.size())
                chunk |= static_cast<uint32_t>(bytes[i + 2]);

            out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
            out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
            out.push_back(i + 1 < bytes.size() ? kAlphabet[(chunk >> 6) & 0x3F] : '=');
            out.push_back(i + 2 < bytes.size() ? kAlphabet[chunk & 0x3F] : '=');
        }
        return out;
    }

    template <typename T>
    void AppendRaw(std::vector<std::byte>& blob, const T& value)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(&value);
        blob.insert(blob.end(), begin, begin + sizeof(T));
    }

    // One unit quad in the XY plane, +Z normal, U along +X and V along +Y:
    //   positions (0,0,0) (1,0,0) (1,1,0) (0,1,0)
    //   uvs       (0,0)   (1,0)   (1,1)   (0,1)
    //   authored tangent stream (when referenced): (0,1,0,-1)
    //   indices   0 1 2  0 2 3   (u16)
    // Regions: pos @0 x48, nrm @48 x48, uv @96 x32, tan @128 x64, idx @192 x12.
    std::vector<std::byte> BuildQuadBlob()
    {
        std::vector<std::byte> blob;
        const float positions[] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
        const float normals[] = { 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 };
        const float uvs[] = { 0, 0, 1, 0, 1, 1, 0, 1 };
        const float tangents[] = { 0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1, 0, 1, 0, -1 };
        const uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
        for (float f : positions) AppendRaw(blob, f);
        for (float f : normals) AppendRaw(blob, f);
        for (float f : uvs) AppendRaw(blob, f);
        for (float f : tangents) AppendRaw(blob, f);
        for (uint16_t i : indices) AppendRaw(blob, i);
        return blob;
    }

    // One node placing mesh 0 at the origin, named like the quad mesh.
    constexpr std::string_view kQuadNode = R"([{"name":"Quad","mesh":0}])";

    // Accessor indices into the fixture: 0=pos, 1=nrm, 2=uv, 3=tan, 4=idx.
    std::string QuadGltfSkeleton(std::string_view meshesJson, std::string_view bufferJson,
                                 std::string_view nodesJson = kQuadNode)
    {
        return std::string(R"({"asset":{"version":"2.0"},)")
            + R"("nodes":)" + std::string(nodesJson) + ","
            + R"("buffers":[)" + std::string(bufferJson) + R"(],)"
            + R"("bufferViews":[)"
              R"({"buffer":0,"byteOffset":0,"byteLength":48},)"
              R"({"buffer":0,"byteOffset":48,"byteLength":48},)"
              R"({"buffer":0,"byteOffset":96,"byteLength":32},)"
              R"({"buffer":0,"byteOffset":128,"byteLength":64},)"
              R"({"buffer":0,"byteOffset":192,"byteLength":12}],)"
              R"("accessors":[)"
              R"({"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
              R"({"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"},)"
              R"({"bufferView":2,"componentType":5126,"count":4,"type":"VEC2"},)"
              R"({"bufferView":3,"componentType":5126,"count":4,"type":"VEC4"},)"
              R"({"bufferView":4,"componentType":5123,"count":6,"type":"SCALAR"}],)"
            + R"("meshes":[)" + std::string(meshesJson) + "]}";
    }

    std::string QuadGltf(std::string_view meshesJson, std::string_view nodesJson = kQuadNode)
    {
        const std::vector<std::byte> blob = BuildQuadBlob();
        const std::string buffer =
            R"({"byteLength":204,"uri":"data:application/octet-stream;base64,)"
            + Base64Encode(blob) + R"("})";
        return QuadGltfSkeleton(meshesJson, buffer, nodesJson);
    }

    std::span<const std::byte> AsBytes(const std::string& text)
    {
        return { reinterpret_cast<const std::byte*>(text.data()), text.size() };
    }

    // GLB container: 12-byte header, JSON chunk padded with spaces, BIN
    // chunk padded with zeros.
    std::vector<std::byte> BuildGlb(std::string json, std::vector<std::byte> bin)
    {
        while (json.size() % 4 != 0)
            json.push_back(' ');
        while (bin.size() % 4 != 0)
            bin.push_back(std::byte{ 0 });

        std::vector<std::byte> glb;
        AppendRaw(glb, uint32_t{ 0x46546C67 }); // "glTF"
        AppendRaw(glb, uint32_t{ 2 });
        AppendRaw(glb, static_cast<uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
        AppendRaw(glb, static_cast<uint32_t>(json.size()));
        AppendRaw(glb, uint32_t{ 0x4E4F534A }); // "JSON"
        glb.insert(glb.end(),
                   reinterpret_cast<const std::byte*>(json.data()),
                   reinterpret_cast<const std::byte*>(json.data()) + json.size());
        AppendRaw(glb, static_cast<uint32_t>(bin.size()));
        AppendRaw(glb, uint32_t{ 0x004E4942 }); // "BIN\0"
        glb.insert(glb.end(), bin.begin(), bin.end());
        return glb;
    }

    constexpr std::string_view kQuadMeshNoTangents =
        R"({"name":"Quad","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4}]})";
} // namespace

// -- ImportGltfScene: the pure stage half -------------------------------------

TEST(MeshCook, QuadWithUvsGetsMikkTSpaceTangents)
{
    const std::string gltf = QuadGltf(kQuadMeshNoTangents);

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    ASSERT_EQ(scene.Meshes.size(), 1u);
    EXPECT_EQ(scene.Meshes[0].Name, "Quad");

    const MeshGeometry& mesh = scene.Meshes[0].Geometry;
    // The de-index/weld round trip must not duplicate the flat quad.
    ASSERT_EQ(mesh.Vertices.size(), 4u);
    ASSERT_EQ(mesh.Indices.size(), 6u);
    ASSERT_EQ(mesh.Sections.size(), 1u);

    // MikkTSpace runs on the source, then the half turn into the engine frame
    // carries its result: U runs along -X and V along +Y with N = -Z. The
    // tangent follows U and the bitangent w * cross(N, T) follows V.
    for (const StaticMeshVertex& vertex : mesh.Vertices)
    {
        EXPECT_NEAR(vertex.Tangent.X, -1.0f, 1e-4f);
        EXPECT_NEAR(vertex.Tangent.Y, 0.0f, 1e-4f);
        EXPECT_NEAR(vertex.Tangent.Z, 0.0f, 1e-4f);
        const Vec3d tangent(vertex.Tangent.X, vertex.Tangent.Y, vertex.Tangent.Z);
        const Vec3d bitangent = vertex.Normal.Cross(tangent) * vertex.Tangent.W;
        EXPECT_NEAR(bitangent.X, 0.0f, 1e-4f);
        EXPECT_NEAR(bitangent.Y, 1.0f, 1e-4f);
        EXPECT_NEAR(bitangent.Z, 0.0f, 1e-4f);
    }
}

TEST(MeshCook, AuthoredTangentsPassThrough)
{
    const std::string gltf = QuadGltf(
        R"({"name":"Quad","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3},"indices":4}]})");

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    ASSERT_EQ(scene.Meshes.size(), 1u);

    // The authored stream is kept, turned into the engine frame with the
    // rest of the mesh; (0,1,0) lies on the turn's axis.
    for (const StaticMeshVertex& vertex : scene.Meshes[0].Geometry.Vertices)
    {
        EXPECT_NEAR(vertex.Tangent.X, 0.0f, 1e-6f);
        EXPECT_EQ(vertex.Tangent.Y, 1.0f);
        EXPECT_NEAR(vertex.Tangent.Z, 0.0f, 1e-6f);
        EXPECT_EQ(vertex.Tangent.W, -1.0f);
    }
}

TEST(MeshCook, UvLessQuadGetsSynthesizedTangents)
{
    const std::string gltf = QuadGltf(
        R"({"name":"Quad","primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":4}]})");

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    ASSERT_EQ(scene.Meshes.size(), 1u);

    // No texture space exists; the format invariant (finite tangent,
    // w == ±1, perpendicular to the normal) must still hold.
    for (const StaticMeshVertex& vertex : scene.Meshes[0].Geometry.Vertices)
    {
        EXPECT_TRUE(vertex.Tangent.W == 1.0f || vertex.Tangent.W == -1.0f);
        const float dot = vertex.Tangent.X * vertex.Normal.X
            + vertex.Tangent.Y * vertex.Normal.Y
            + vertex.Tangent.Z * vertex.Normal.Z;
        EXPECT_NEAR(dot, 0.0f, 1e-4f);
    }
}

TEST(MeshCook, MultiplePrimitivesBecomeSections)
{
    const std::string gltf = QuadGltf(
        R"({"name":"TwoPrims","primitives":[)"
        R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4},)"
        R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4}]})");

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    ASSERT_EQ(scene.Meshes.size(), 1u);

    const MeshGeometry& mesh = scene.Meshes[0].Geometry;
    ASSERT_EQ(mesh.Sections.size(), 2u);
    EXPECT_EQ(mesh.Sections[0].MaterialSlot, 0u);
    EXPECT_EQ(mesh.Sections[1].MaterialSlot, 1u);
    EXPECT_EQ(mesh.Sections[1].VertexOffset, mesh.Sections[0].VertexCount);
    EXPECT_EQ(mesh.Sections[1].IndexOffset, mesh.Sections[0].IndexCount);
    EXPECT_EQ(mesh.Vertices.size(), 8u);
    EXPECT_EQ(mesh.Indices.size(), 12u);
}

TEST(MeshCook, GlbContainerParses)
{
    const std::string json = QuadGltfSkeleton(kQuadMeshNoTangents, R"({"byteLength":204})");
    const std::vector<std::byte> glb = BuildGlb(json, BuildQuadBlob());

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(glb, scene, &error)) << error;
    ASSERT_EQ(scene.Meshes.size(), 1u);
    EXPECT_EQ(scene.Meshes[0].Geometry.Vertices.size(), 4u);
}

TEST(MeshCook, ExternalBufferUriIsRejected)
{
    const std::string gltf = QuadGltfSkeleton(
        kQuadMeshNoTangents, R"({"byteLength":204,"uri":"external.bin"})");

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("external buffer"), std::string::npos) << error;
}

TEST(MeshCook, MalformedBytesAreRejected)
{
    const std::string garbage = "this is not gltf at all";
    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(garbage), scene, &error));
    EXPECT_FALSE(error.empty());
}

TEST(MeshCook, NonTrianglePrimitiveIsRejected)
{
    const std::string gltf = QuadGltf(
        R"({"name":"Lines","primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":4,"mode":1}]})");

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("triangle"), std::string::npos) << error;
}

TEST(MeshCook, MissingNormalsAreRejected)
{
    const std::string gltf = QuadGltf(
        R"({"name":"NoNormals","primitives":[{"attributes":{"POSITION":0},"indices":4}]})");

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("NORMAL"), std::string::npos) << error;
}

// -- Engine frame and node placement ---------------------------------------------

namespace
{
    // Every corner of every triangle, in index order: what a mesh draws,
    // independent of how vertices happen to be shared.
    std::vector<Vec3d> Corners(const MeshGeometry& mesh)
    {
        std::vector<Vec3d> corners;
        for (const uint32_t index : mesh.Indices)
            corners.push_back(mesh.Vertices[index].Position);
        return corners;
    }

    // The face normal the winding implies (counter-clockwise front faces).
    Vec3d WindingNormal(const MeshGeometry& mesh, std::size_t triangle)
    {
        const Vec3d& a = mesh.Vertices[mesh.Indices[triangle * 3 + 0]].Position;
        const Vec3d& b = mesh.Vertices[mesh.Indices[triangle * 3 + 1]].Position;
        const Vec3d& c = mesh.Vertices[mesh.Indices[triangle * 3 + 2]].Position;
        return (b - a).Cross(c - a).Normalized();
    }

    void ExpectFrontFacesAgreeWithNormals(const MeshGeometry& mesh)
    {
        for (std::size_t triangle = 0; triangle * 3 < mesh.Indices.size(); ++triangle)
        {
            const Vec3d winding = WindingNormal(mesh, triangle);
            for (int corner = 0; corner < 3; ++corner)
                EXPECT_GT(winding.Dot(mesh.Vertices[mesh.Indices[triangle * 3 + corner]].Normal), 0.99f)
                    << "triangle " << triangle;
        }
    }

    // The quad's corners in glTF order: 0 1 2 0 2 3.
    const std::vector<Vec3d> kQuadCorners{
        Vec3d(0, 0, 0), Vec3d(1, 0, 0), Vec3d(1, 1, 0),
        Vec3d(0, 0, 0), Vec3d(1, 1, 0), Vec3d(0, 1, 0) };

    constexpr std::string_view kQuadMeshWithTangents =
        R"({"name":"Quad","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3},"indices":4}]})";
}

// The quad faces glTF's front (+Z). Imported, it faces the engine's forward
// (-Z), which is the same half turn about +Y for positions, normals and
// tangents, with the winding still agreeing with the normals.
TEST(MeshCook, GltfFrontFacesEngineForward)
{
    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(QuadGltf(kQuadMeshWithTangents)), scene, &error)) << error;
    ASSERT_EQ(scene.Meshes.size(), 1u);
    const MeshGeometry& mesh = scene.Meshes[0].Geometry;

    const std::vector<Vec3d> corners = Corners(mesh);
    ASSERT_EQ(corners.size(), kQuadCorners.size());
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        EXPECT_NEAR(corners[i].X, -kQuadCorners[i].X, 1e-6f) << "corner " << i;
        EXPECT_NEAR(corners[i].Y, kQuadCorners[i].Y, 1e-6f) << "corner " << i;
        EXPECT_NEAR(corners[i].Z, -kQuadCorners[i].Z, 1e-6f) << "corner " << i;
    }
    for (const StaticMeshVertex& vertex : mesh.Vertices)
    {
        EXPECT_NEAR(vertex.Normal.X, 0.0f, 1e-6f);
        EXPECT_NEAR(vertex.Normal.Y, 0.0f, 1e-6f);
        EXPECT_NEAR(vertex.Normal.Z, -1.0f, 1e-6f);
        EXPECT_EQ(vertex.Tangent.W, -1.0f);
    }
    ExpectFrontFacesAgreeWithNormals(mesh);
}

// A static mesh is baked through its node's world transform, parents
// included, then turned into the engine frame.
TEST(MeshCook, StaticNodeWorldTransformIsBakedIntoTheMesh)
{
    const Quat<float> turn = Quat<float>::FromAxisAngle(Vec3d(0, 1, 0), std::numbers::pi_v<float> / 2.0f);
    const std::string nodes = std::format(
        R"([{{"name":"Group","children":[1],"translation":[0,0,3]}},)"
        R"({{"name":"Quad","mesh":0,"translation":[5,0,0],"rotation":[{},{},{},{}],"scale":[2,2,2]}}])",
        turn.X, turn.Y, turn.Z, turn.W);

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(QuadGltf(kQuadMeshWithTangents, nodes)), scene, &error)) << error;
    ASSERT_EQ(scene.Meshes.size(), 1u);
    const MeshGeometry& mesh = scene.Meshes[0].Geometry;

    const Mat4 world = Mat4::MakeTranslation(0, 0, 3)
        * Transform3f{ Vec3d(5, 0, 0), turn, Vec3d(2, 2, 2) }.ToMat4();
    Mat4 engineFrame = Mat4::Identity();
    engineFrame.Data[0][0] = -1.0f;
    engineFrame.Data[2][2] = -1.0f;
    const Mat4 toModel = engineFrame * world;

    const std::vector<Vec3d> corners = Corners(mesh);
    ASSERT_EQ(corners.size(), kQuadCorners.size());
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        const Vec4 expected = toModel * Vec4(kQuadCorners[i].X, kQuadCorners[i].Y, kQuadCorners[i].Z, 1.0f);
        EXPECT_NEAR(corners[i].X, expected.X, 1e-5f) << "corner " << i;
        EXPECT_NEAR(corners[i].Y, expected.Y, 1e-5f) << "corner " << i;
        EXPECT_NEAR(corners[i].Z, expected.Z, 1e-5f) << "corner " << i;
    }
    // +Z turned a quarter about Y is +X; the engine frame's half turn makes it -X.
    for (const StaticMeshVertex& vertex : mesh.Vertices)
    {
        EXPECT_NEAR(vertex.Normal.X, -1.0f, 1e-5f);
        EXPECT_NEAR(vertex.Normal.Z, 0.0f, 1e-5f);
    }
    ExpectFrontFacesAgreeWithNormals(mesh);
}

// A mirroring node flips winding and tangent handedness together, so front
// faces stay front faces and the bitangent still follows V.
TEST(MeshCook, MirroredNodeKeepsFrontFacesAndBitangents)
{
    ImportedGltfScene plain;
    ImportedGltfScene mirrored;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(QuadGltf(kQuadMeshNoTangents)), plain, &error)) << error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(QuadGltf(kQuadMeshNoTangents,
                                                 R"([{"name":"Quad","mesh":0,"scale":[-1,1,1]}])")),
                                mirrored, &error)) << error;

    const MeshGeometry& mesh = mirrored.Meshes.at(0).Geometry;
    ExpectFrontFacesAgreeWithNormals(mesh);
    for (const StaticMeshVertex& vertex : mesh.Vertices)
    {
        EXPECT_EQ(vertex.Tangent.W, -plain.Meshes.at(0).Geometry.Vertices.at(0).Tangent.W);
        const Vec3d tangent(vertex.Tangent.X, vertex.Tangent.Y, vertex.Tangent.Z);
        const Vec3d bitangent = vertex.Normal.Cross(tangent) * vertex.Tangent.W;
        EXPECT_NEAR(bitangent.Y, 1.0f, 1e-4f);
    }
}

// Exporters write zero tangents for degenerate UVs. The bake turns them with
// the mesh and leaves them zero rather than failing or inventing a direction.
TEST(MeshCook, ZeroLengthAuthoredTangentSurvivesTheBake)
{
    std::vector<std::byte> blob = BuildQuadBlob();
    const float zeroTangent[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::memcpy(blob.data() + 128, zeroTangent, sizeof(zeroTangent)); // vertex 0's tangent
    const std::string buffer = R"({"byteLength":204,"uri":"data:application/octet-stream;base64,)"
        + Base64Encode(blob) + R"("})";

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(QuadGltfSkeleton(kQuadMeshWithTangents, buffer)), scene, &error))
        << error;
    const MeshGeometry& mesh = scene.Meshes.at(0).Geometry;
    EXPECT_EQ(mesh.Vertices.at(0).Tangent.X, 0.0f);
    EXPECT_EQ(mesh.Vertices.at(0).Tangent.Y, 0.0f);
    EXPECT_EQ(mesh.Vertices.at(0).Tangent.Z, 0.0f);
    EXPECT_NEAR(mesh.Vertices.at(1).Tangent.Y, 1.0f, 1e-6f);
}

TEST(MeshCook, MeshNoNodePlacesIsRejected)
{
    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(QuadGltf(kQuadMeshNoTangents, "[]")), scene, &error));
    EXPECT_NE(error.find("'Quad'"), std::string::npos) << error;
    EXPECT_NE(error.find("not placed"), std::string::npos) << error;
}

// -- GltfMeshImporter: artifacts -----------------------------------------------

TEST(MeshCook, SingleMeshArtifactKeepsSourceVirtualPath)
{
    const std::string gltf = QuadGltf(kQuadMeshNoTangents);

    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result =
        importer.Import(ImportInput{ "meshes/quad.gltf", AsBytes(gltf) }, output);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    ASSERT_EQ(result.Artifacts.size(), 1u);
    EXPECT_EQ(result.Artifacts[0].Path, "asset://meshes/quad.gltf");
    EXPECT_EQ(result.Artifacts[0].FileRelPath, ".cooked/meshes/quad.gltf.smesh");
    EXPECT_EQ(result.Artifacts[0].Type, AssetType::StaticMesh);

    // The cooked bytes round-trip the runtime loader (and are therefore
    // format version 2 with valid tangents).
    LoggingProvider logging;
    MeshLoader loader(logging);
    MeshGeometry loaded;
    ASSERT_TRUE(loader.LoadFromBytes(output.Files.at(".cooked/meshes/quad.gltf.smesh"), loaded));
    EXPECT_EQ(loaded.Vertices.size(), 4u);
    EXPECT_EQ(loaded.Indices.size(), 6u);
}

TEST(MeshCook, MultiMeshSourceEmitsFragmentNamedArtifacts)
{
    const std::string gltf = QuadGltf(
        R"({"name":"MeshA","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4}]},)"
        R"({"name":"MeshB","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4}]})",
        R"([{"name":"A","mesh":0},{"name":"B","mesh":1}])");

    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result =
        importer.Import(ImportInput{ "meshes/props.gltf", AsBytes(gltf) }, output);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    ASSERT_EQ(result.Artifacts.size(), 2u);
    EXPECT_EQ(result.Artifacts[0].Path, "asset://meshes/props.gltf#A");
    EXPECT_EQ(result.Artifacts[0].FileRelPath, ".cooked/meshes/props.gltf.A.smesh");
    EXPECT_EQ(result.Artifacts[1].Path, "asset://meshes/props.gltf#B");
    EXPECT_EQ(result.Artifacts[1].FileRelPath, ".cooked/meshes/props.gltf.B.smesh");
    EXPECT_TRUE(output.Files.contains(".cooked/meshes/props.gltf.A.smesh"));
    EXPECT_TRUE(output.Files.contains(".cooked/meshes/props.gltf.B.smesh"));
}

// Two placements of one mesh are two artifacts, named by their nodes; an
// unnamed node takes its index.
TEST(MeshCook, StaticArtifactsAreNamedByTheirNodes)
{
    const std::string gltf = QuadGltf(kQuadMeshNoTangents,
                                      R"([{"name":"Chair.L","mesh":0},{"mesh":0,"translation":[4,0,0]}])");

    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result = importer.Import(ImportInput{ "meshes/set.gltf", AsBytes(gltf) }, output);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    ASSERT_EQ(result.Artifacts.size(), 2u);
    EXPECT_EQ(result.Artifacts[0].Path, "asset://meshes/set.gltf#Chair_L");
    EXPECT_EQ(result.Artifacts[1].Path, "asset://meshes/set.gltf#node1");
}

// Names that sanitize alike would otherwise be told apart by discovery
// order, which moves when the source changes.
TEST(MeshCook, NodeNamesThatSanitizeAlikeAreRejected)
{
    const std::string gltf = QuadGltf(kQuadMeshNoTangents,
                                      R"([{"name":"Arm.L","mesh":0},{"name":"Arm_L","mesh":0}])");

    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result = importer.Import(ImportInput{ "meshes/set.gltf", AsBytes(gltf) }, output);
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("node 0 'Arm.L'"), std::string::npos) << result.Error;
    EXPECT_NE(result.Error.find("node 1 'Arm_L'"), std::string::npos) << result.Error;
}

TEST(MeshCook, ImportIsDeterministic)
{
    const std::string gltf = QuadGltf(kQuadMeshNoTangents);

    GltfMeshImporter importer;
    MemoryCookOutputWriter first;
    MemoryCookOutputWriter second;
    ASSERT_TRUE(importer.Import(ImportInput{ "m/q.gltf", AsBytes(gltf) }, first).IsValid());
    ASSERT_TRUE(importer.Import(ImportInput{ "m/q.gltf", AsBytes(gltf) }, second).IsValid());

    EXPECT_EQ(first.Files, second.Files);
}

// -- Format version gate --------------------------------------------------------

TEST(MeshCook, LoaderRejectsVersionOneSmesh)
{
    // A v2 cook output with the version field patched back to 1 must be
    // rejected by version, not parsed as stale-layout bytes.
    const std::string gltf = QuadGltf(kQuadMeshNoTangents);
    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    ASSERT_TRUE(importer.Import(ImportInput{ "m/q.gltf", AsBytes(gltf) }, output).IsValid());

    std::vector<std::byte> bytes = output.Files.at(".cooked/m/q.gltf.smesh");
    const uint32_t versionOne = 1;
    std::memcpy(bytes.data() + 4, &versionOne, sizeof(versionOne)); // after "SMSH"

    LoggingProvider logging;
    MeshLoader loader(logging);
    MeshGeometry loaded;
    EXPECT_FALSE(loader.LoadFromBytes(bytes, loaded));
}

// -- Blender front end -----------------------------------------------------------

namespace
{
    // The Blender front end shells out by design, so these tests need the real
    // executable. Skipping keeps the suite green on a machine without it.
    [[nodiscard]] bool BlenderAvailable()
    {
#ifdef _WIN32
        constexpr const char* kProbe = "blender --version > NUL 2>&1";
#else
        constexpr const char* kProbe = "blender --version > /dev/null 2>&1";
#endif
        return std::getenv("SENCHA_BLENDER") != nullptr || std::system(kProbe) == 0;
    }

    // Authors a .blend with Blender itself and returns its bytes. `setupPython`
    // runs against the factory-default scene (one mesh, "Cube") before the save,
    // so a case describes only what it adds.
    [[nodiscard]] bool AuthorBlendFile(const std::filesystem::path& blendPath,
                                       std::string_view setupPython,
                                       std::vector<std::byte>& outBytes)
    {
        const std::string command =
            "blender --background --factory-startup --python-exit-code 1 --python-expr \""
            + std::string(setupPython)
            + "import bpy; bpy.ops.wm.save_as_mainfile(filepath=r'"
            + blendPath.generic_string() +
#ifdef _WIN32
            "')\" > NUL 2>&1";
#else
            "')\" > /dev/null 2>&1";
#endif
        if (std::system(command.c_str()) != 0)
            return false;

        std::ifstream file(blendPath, std::ios::binary);
        if (!file.is_open())
            return false;
        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        outBytes.resize(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(outBytes.data()), size);
        return file.good();
    }

    // Scoped temp directory for a .blend fixture.
    struct ScopedTestDir
    {
        std::filesystem::path Path;

        ScopedTestDir()
            : Path(std::filesystem::temp_directory_path()
                   / ("sencha_blend_test_"
                      + std::to_string(std::random_device{}())))
        {
            std::filesystem::create_directories(Path);
        }

        ~ScopedTestDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(Path, ec);
        }
    };
} // namespace

// The real probe names the Blender that would cook, so the cook identity
// moves when that Blender does.
TEST(MeshCook, TheBlendToolchainProbeNamesTheInstalledBlender)
{
    if (!BlenderAvailable())
        GTEST_SKIP() << "Blender not installed; .blend cook is a dev-machine-optional path";
    const std::string toolchain = ProbeBlendToolchain();
    EXPECT_EQ(toolchain.rfind("blender ", 0), 0u) << toolchain;
    EXPECT_NE(toolchain.find(" gltf "), std::string::npos) << toolchain;
}

TEST(MeshCook, BlendImportsThroughHeadlessBlender)
{
    if (!BlenderAvailable())
        GTEST_SKIP() << "Blender not installed; .blend cook is a dev-machine-optional path";

    ScopedTestDir dir;
    std::vector<std::byte> blendBytes;
    ASSERT_TRUE(AuthorBlendFile(dir.Path / "scene.blend", "", blendBytes));

    BlendMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result =
        importer.Import(ImportInput{ "meshes/scene.blend", blendBytes }, output);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    ASSERT_EQ(result.Artifacts.size(), 1u);
    EXPECT_EQ(result.Artifacts[0].Path, "asset://meshes/scene.blend");
    EXPECT_EQ(result.Artifacts[0].Type, AssetType::StaticMesh);

    LoggingProvider logging;
    MeshLoader loader(logging);
    MeshGeometry loaded;
    ASSERT_TRUE(loader.LoadFromBytes(
        output.Files.at(".cooked/meshes/scene.blend.smesh"), loaded));
    EXPECT_EQ(loaded.Vertices.size() % 4, 0u); // a cube: 6 quads, welded corners
    EXPECT_GE(loaded.Indices.size(), 36u);
}

// A rigged .blend must reach the runtime as a skinned mesh. The cook exports
// tangents only for a skinned source, because the glTF importer rejects a
// skinned primitive without authored TANGENT (cook-side MikkTSpace re-welds
// vertices, which would desync the parallel influence stream). Without that
// export the import fails outright, so this covers the asymmetry from the
// authored file down to the loaded influences.
TEST(MeshCook, RiggedBlendImportsAsASkinnedMesh)
{
    if (!BlenderAvailable())
        GTEST_SKIP() << "Blender not installed; .blend cook is a dev-machine-optional path";

    ScopedTestDir dir;
    std::vector<std::byte> blendBytes;
    ASSERT_TRUE(AuthorBlendFile(
        dir.Path / "rigged.blend",
        // Bind the factory cube to a one-bone armature with automatic weights.
        "import bpy; "
        "bpy.ops.object.armature_add(enter_editmode=False, location=(0,0,-1)); "
        "arm=bpy.context.object; "
        "cube=bpy.data.objects['Cube']; "
        "cube.select_set(True); "
        "bpy.context.view_layer.objects.active=arm; "
        "bpy.ops.object.parent_set(type='ARMATURE_AUTO'); ",
        blendBytes));

    BlendMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result =
        importer.Import(ImportInput{ "meshes/rigged.blend", blendBytes }, output);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    // A skeleton and a skinned mesh, each '#'-suffixed (a skinned source is
    // never the single-static-mesh case that keeps the bare source path).
    const CookedArtifact* skeleton = nullptr;
    const CookedArtifact* mesh = nullptr;
    for (const CookedArtifact& artifact : result.Artifacts)
    {
        if (artifact.Type == AssetType::Skeleton)
            skeleton = &artifact;
        else if (artifact.Type == AssetType::SkinnedMesh)
            mesh = &artifact;
    }
    ASSERT_NE(skeleton, nullptr);
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(skeleton->FileRelPath.ends_with(".sskel"));
    EXPECT_TRUE(mesh->FileRelPath.ends_with(".skmesh"));

    LoggingProvider logging;
    MeshLoader loader(logging);
    SkinnedMeshData loaded;
    ASSERT_TRUE(loader.LoadSkinnedFromBytes(output.Files.at(mesh->FileRelPath), loaded));
    EXPECT_EQ(loaded.Skinning.Influences.size(), loaded.Geometry.Vertices.size());
    EXPECT_EQ(loaded.Skinning.SkeletonPath, skeleton->Path);
    EXPECT_GT(loaded.Skinning.JointCount, 0u);

    // Every vertex is bound: the cook normalizes weights to sum to 255, so a
    // vertex the exporter left unweighted would show up as a zero row here.
    for (const MeshSkinInfluence& influence : loaded.Skinning.Influences)
    {
        const int total = influence.Weights[0] + influence.Weights[1]
            + influence.Weights[2] + influence.Weights[3];
        ASSERT_EQ(total, 255);
    }
}

#endif // SENCHA_ENABLE_COOK
