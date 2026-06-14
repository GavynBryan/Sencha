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

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
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

    // Accessor indices into the fixture: 0=pos, 1=nrm, 2=uv, 3=tan, 4=idx.
    std::string QuadGltfSkeleton(std::string_view meshesJson, std::string_view bufferJson)
    {
        return std::string(R"({"asset":{"version":"2.0"},)")
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

    std::string QuadGltf(std::string_view meshesJson)
    {
        const std::vector<std::byte> blob = BuildQuadBlob();
        const std::string buffer =
            R"({"byteLength":204,"uri":"data:application/octet-stream;base64,)"
            + Base64Encode(blob) + R"("})";
        return QuadGltfSkeleton(meshesJson, buffer);
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

// -- ImportGltfMeshes: the pure stage half ------------------------------------

TEST(MeshCook, QuadWithUvsGetsMikkTSpaceTangents)
{
    const std::string gltf = QuadGltf(kQuadMeshNoTangents);

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    ASSERT_TRUE(ImportGltfMeshes(AsBytes(gltf), meshes, &error)) << error;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Name, "Quad");

    const MeshGeometry& mesh = meshes[0].Geometry;
    // The de-index/weld round trip must not duplicate the flat quad.
    ASSERT_EQ(mesh.Vertices.size(), 4u);
    ASSERT_EQ(mesh.Indices.size(), 6u);
    ASSERT_EQ(mesh.Sections.size(), 1u);

    // U runs along +X with N = +Z, so MikkTSpace must produce T = +X with
    // w = +1 (bitangent = cross(N,T)*w = +Y = the V direction).
    for (const StaticMeshVertex& vertex : mesh.Vertices)
    {
        EXPECT_NEAR(vertex.Tangent.X, 1.0f, 1e-4f);
        EXPECT_NEAR(vertex.Tangent.Y, 0.0f, 1e-4f);
        EXPECT_NEAR(vertex.Tangent.Z, 0.0f, 1e-4f);
        EXPECT_EQ(vertex.Tangent.W, 1.0f);
    }
}

TEST(MeshCook, AuthoredTangentsPassThrough)
{
    const std::string gltf = QuadGltf(
        R"({"name":"Quad","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"TANGENT":3},"indices":4}]})");

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    ASSERT_TRUE(ImportGltfMeshes(AsBytes(gltf), meshes, &error)) << error;
    ASSERT_EQ(meshes.size(), 1u);

    for (const StaticMeshVertex& vertex : meshes[0].Geometry.Vertices)
    {
        EXPECT_EQ(vertex.Tangent.X, 0.0f);
        EXPECT_EQ(vertex.Tangent.Y, 1.0f);
        EXPECT_EQ(vertex.Tangent.Z, 0.0f);
        EXPECT_EQ(vertex.Tangent.W, -1.0f);
    }
}

TEST(MeshCook, UvLessQuadGetsSynthesizedTangents)
{
    const std::string gltf = QuadGltf(
        R"({"name":"Quad","primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":4}]})");

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    ASSERT_TRUE(ImportGltfMeshes(AsBytes(gltf), meshes, &error)) << error;
    ASSERT_EQ(meshes.size(), 1u);

    // No texture space exists; the format invariant (finite tangent,
    // w == ±1, perpendicular to the normal) must still hold.
    for (const StaticMeshVertex& vertex : meshes[0].Geometry.Vertices)
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

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    ASSERT_TRUE(ImportGltfMeshes(AsBytes(gltf), meshes, &error)) << error;
    ASSERT_EQ(meshes.size(), 1u);

    const MeshGeometry& mesh = meshes[0].Geometry;
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

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    ASSERT_TRUE(ImportGltfMeshes(glb, meshes, &error)) << error;
    ASSERT_EQ(meshes.size(), 1u);
    EXPECT_EQ(meshes[0].Geometry.Vertices.size(), 4u);
}

TEST(MeshCook, ExternalBufferUriIsRejected)
{
    const std::string gltf = QuadGltfSkeleton(
        kQuadMeshNoTangents, R"({"byteLength":204,"uri":"external.bin"})");

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    EXPECT_FALSE(ImportGltfMeshes(AsBytes(gltf), meshes, &error));
    EXPECT_NE(error.find("external buffer"), std::string::npos) << error;
}

TEST(MeshCook, MalformedBytesAreRejected)
{
    const std::string garbage = "this is not gltf at all";
    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    EXPECT_FALSE(ImportGltfMeshes(AsBytes(garbage), meshes, &error));
    EXPECT_FALSE(error.empty());
}

TEST(MeshCook, NonTrianglePrimitiveIsRejected)
{
    const std::string gltf = QuadGltf(
        R"({"name":"Lines","primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":4,"mode":1}]})");

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    EXPECT_FALSE(ImportGltfMeshes(AsBytes(gltf), meshes, &error));
    EXPECT_NE(error.find("triangle"), std::string::npos) << error;
}

TEST(MeshCook, MissingNormalsAreRejected)
{
    const std::string gltf = QuadGltf(
        R"({"name":"NoNormals","primitives":[{"attributes":{"POSITION":0},"indices":4}]})");

    std::vector<ImportedGltfMesh> meshes;
    std::string error;
    EXPECT_FALSE(ImportGltfMeshes(AsBytes(gltf), meshes, &error));
    EXPECT_NE(error.find("NORMAL"), std::string::npos) << error;
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
        R"({"name":"A","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4}]},)"
        R"({"name":"B","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":4}]})");

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

TEST(MeshCook, BlendImportsThroughHeadlessBlender)
{
#ifdef _WIN32
    constexpr const char* kProbe = "blender --version > NUL 2>&1";
#else
    constexpr const char* kProbe = "blender --version > /dev/null 2>&1";
#endif
    if (std::getenv("SENCHA_BLENDER") == nullptr && std::system(kProbe) != 0)
        GTEST_SKIP() << "Blender not installed; .blend cook is a dev-machine-optional path";

    // Author a .blend with Blender itself: the factory default scene
    // contains one mesh ("Cube").
    std::random_device rd;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("sencha_blend_test_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    const std::filesystem::path blendPath = dir / "scene.blend";

    const std::string saveCommand =
        "blender --background --factory-startup --python-exit-code 1 --python-expr "
        "\"import bpy; bpy.ops.wm.save_as_mainfile(filepath=r'"
        + blendPath.generic_string() +
#ifdef _WIN32
        "')\" > NUL 2>&1";
#else
        "')\" > /dev/null 2>&1";
#endif
    ASSERT_EQ(std::system(saveCommand.c_str()), 0);

    std::ifstream file(blendPath, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> blendBytes(static_cast<std::size_t>(size));
    file.read(reinterpret_cast<char*>(blendBytes.data()), size);
    ASSERT_TRUE(file.good());

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

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

#endif // SENCHA_ENABLE_COOK
