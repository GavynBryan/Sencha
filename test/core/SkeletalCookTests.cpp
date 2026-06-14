// Stage 5 (docs/assets/pipeline.md, Decisions J, M, N): the glTF skin /
// animation cook. Zero-thread, no filesystem — a skinned, animated glTF is
// built in memory (data-URI buffer) and the importer writes to a memory
// output. The cooked artifacts are round-tripped back through the runtime
// loaders so the cook→load chain is exercised end to end.

#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <assets/animation/AnimationClipSerializer.h>
#include <assets/cook/MeshCook.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <assets/static_mesh/MeshLoader.h>
#include <core/logging/LoggingProvider.h>

#include <cstdint>
#include <map>
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
            if (i + 1 < bytes.size()) chunk |= static_cast<uint32_t>(bytes[i + 1]) << 8;
            if (i + 2 < bytes.size()) chunk |= static_cast<uint32_t>(bytes[i + 2]);
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

    struct BlobBuilder
    {
        std::vector<std::byte> Data;

        struct View { std::size_t Offset; std::size_t Length; };

        template <typename T>
        View Add(const std::vector<T>& values)
        {
            while (Data.size() % 4 != 0)
                Data.push_back(std::byte{ 0 });
            const std::size_t offset = Data.size();
            for (const T& v : values)
                AppendRaw(Data, v);
            return { offset, Data.size() - offset };
        }
    };

    std::span<const std::byte> AsBytes(const std::string& text)
    {
        return { reinterpret_cast<const std::byte*>(text.data()), text.size() };
    }

    // A two-joint rig (root, child at +Y) skinning one triangle, plus a
    // two-key rotation animation on the child. Skin joints are listed
    // [root, child]; the child node is the root node's child, so the cook's
    // topological order keeps them in [root, child] too.
    std::string BuildSkinnedAnimatedGltf()
    {
        BlobBuilder blob;
        const auto pos = blob.Add<float>({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
        const auto nrm = blob.Add<float>({ 0, 0, 1, 0, 0, 1, 0, 0, 1 });
        const auto tan = blob.Add<float>({ 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1 });
        const auto joints = blob.Add<uint16_t>({ 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 });
        const auto weights = blob.Add<float>({ 1, 0, 0, 0, 1, 0, 0, 0, 0.5f, 0.5f, 0, 0 });
        const auto idx = blob.Add<uint16_t>({ 0, 1, 2 });
        // Inverse-bind matrices, column-major: identity for both joints.
        const auto ibm = blob.Add<float>({
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
        const auto animIn = blob.Add<float>({ 0.0f, 1.0f });
        const auto animOut = blob.Add<float>({
            0, 0, 0, 1,               // identity
            0, 0, 0.70710678f, 0.70710678f }); // 90° about Z

        const std::string base64 = Base64Encode(blob.Data);

        const auto view = [](const BlobBuilder::View& v) {
            return std::string("{\"buffer\":0,\"byteOffset\":") + std::to_string(v.Offset)
                + ",\"byteLength\":" + std::to_string(v.Length) + "}";
        };

        std::string gltf;
        gltf += R"({"asset":{"version":"2.0"},)";
        gltf += R"("scene":0,"scenes":[{"nodes":[0,2]}],)";
        gltf += R"("nodes":[)"
                R"({"name":"root","children":[1]},)"
                R"({"name":"child","translation":[0,1,0]},)"
                R"({"name":"body","mesh":0,"skin":0}],)";
        gltf += R"("skins":[{"name":"rig","joints":[0,1],"inverseBindMatrices":6}],)";
        gltf += R"("meshes":[{"name":"body","primitives":[{"attributes":{)"
                R"("POSITION":0,"NORMAL":1,"TANGENT":2,"JOINTS_0":3,"WEIGHTS_0":4},"indices":5}]}],)";
        gltf += R"("animations":[{"name":"wave","channels":[)"
                R"({"sampler":0,"target":{"node":1,"path":"rotation"}}],)"
                R"("samplers":[{"input":7,"output":8,"interpolation":"LINEAR"}]}],)";
        gltf += R"("buffers":[{"byteLength":)" + std::to_string(blob.Data.size())
                + R"(,"uri":"data:application/octet-stream;base64,)" + base64 + R"("}],)";
        gltf += R"("bufferViews":[)"
                + view(pos) + "," + view(nrm) + "," + view(tan) + "," + view(joints) + ","
                + view(weights) + "," + view(idx) + "," + view(ibm) + "," + view(animIn) + ","
                + view(animOut) + "],";
        gltf += R"("accessors":[)"
                R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
                R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)"
                R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},)"
                R"({"bufferView":3,"componentType":5123,"count":3,"type":"VEC4"},)"
                R"({"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},)"
                R"({"bufferView":5,"componentType":5123,"count":3,"type":"SCALAR"},)"
                R"({"bufferView":6,"componentType":5126,"count":2,"type":"MAT4"},)"
                R"({"bufferView":7,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},)"
                R"({"bufferView":8,"componentType":5126,"count":2,"type":"VEC4"}]})";
        return gltf;
    }

    // Two single-joint skins, one mesh skinned to skin 0, and one animation
    // whose two channels target a joint in *each* skin. Exercises the
    // importer's "an animation binds to the first skin its channels resolve
    // to; channels targeting another skin are skipped" policy.
    std::string BuildTwoSkinAnimatedGltf()
    {
        BlobBuilder blob;
        const auto pos = blob.Add<float>({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
        const auto nrm = blob.Add<float>({ 0, 0, 1, 0, 0, 1, 0, 0, 1 });
        const auto tan = blob.Add<float>({ 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1 });
        const auto joints = blob.Add<uint16_t>({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
        const auto weights = blob.Add<float>({ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 });
        const auto idx = blob.Add<uint16_t>({ 0, 1, 2 });
        const auto ibm = blob.Add<float>({
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
        const auto animIn = blob.Add<float>({ 0.0f, 1.0f });
        const auto animOut = blob.Add<float>({
            0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f });

        const std::string base64 = Base64Encode(blob.Data);
        const auto view = [](const BlobBuilder::View& v) {
            return std::string("{\"buffer\":0,\"byteOffset\":") + std::to_string(v.Offset)
                + ",\"byteLength\":" + std::to_string(v.Length) + "}";
        };

        std::string gltf;
        gltf += R"({"asset":{"version":"2.0"},)";
        gltf += R"("nodes":[{"name":"j0"},{"name":"j1"},{"name":"body","mesh":0,"skin":0}],)";
        // Both skins share the single identity inverse-bind accessor.
        gltf += R"("skins":[{"name":"rigA","joints":[0],"inverseBindMatrices":6},)"
                R"({"name":"rigB","joints":[1],"inverseBindMatrices":6}],)";
        gltf += R"("meshes":[{"name":"body","primitives":[{"attributes":{)"
                R"("POSITION":0,"NORMAL":1,"TANGENT":2,"JOINTS_0":3,"WEIGHTS_0":4},"indices":5}]}],)";
        // Channel 0 targets skin 0's joint, channel 1 targets skin 1's joint.
        gltf += R"("animations":[{"name":"wave","channels":[)"
                R"({"sampler":0,"target":{"node":0,"path":"rotation"}},)"
                R"({"sampler":1,"target":{"node":1,"path":"rotation"}}],)"
                R"("samplers":[{"input":7,"output":8,"interpolation":"LINEAR"},)"
                R"({"input":7,"output":8,"interpolation":"LINEAR"}]}],)";
        gltf += R"("buffers":[{"byteLength":)" + std::to_string(blob.Data.size())
                + R"(,"uri":"data:application/octet-stream;base64,)" + base64 + R"("}],)";
        gltf += R"("bufferViews":[)"
                + view(pos) + "," + view(nrm) + "," + view(tan) + "," + view(joints) + ","
                + view(weights) + "," + view(idx) + "," + view(ibm) + "," + view(animIn) + ","
                + view(animOut) + "],";
        gltf += R"("accessors":[)"
                R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
                R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)"
                R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},)"
                R"({"bufferView":3,"componentType":5123,"count":3,"type":"VEC4"},)"
                R"({"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},)"
                R"({"bufferView":5,"componentType":5123,"count":3,"type":"SCALAR"},)"
                R"({"bufferView":6,"componentType":5126,"count":1,"type":"MAT4"},)"
                R"({"bufferView":7,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},)"
                R"({"bufferView":8,"componentType":5126,"count":2,"type":"VEC4"}]})";
        return gltf;
    }

    // One mesh referenced by two nodes that carry different skins — no
    // animation needed; the ambiguity is in the mesh→skin pairing.
    std::string BuildMeshInstancedWithTwoSkinsGltf()
    {
        BlobBuilder blob;
        const auto pos = blob.Add<float>({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
        const auto nrm = blob.Add<float>({ 0, 0, 1, 0, 0, 1, 0, 0, 1 });
        const auto tan = blob.Add<float>({ 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1 });
        const auto joints = blob.Add<uint16_t>({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
        const auto weights = blob.Add<float>({ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 });
        const auto idx = blob.Add<uint16_t>({ 0, 1, 2 });
        const auto ibm = blob.Add<float>({
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });

        const std::string base64 = Base64Encode(blob.Data);
        const auto view = [](const BlobBuilder::View& v) {
            return std::string("{\"buffer\":0,\"byteOffset\":") + std::to_string(v.Offset)
                + ",\"byteLength\":" + std::to_string(v.Length) + "}";
        };

        std::string gltf;
        gltf += R"({"asset":{"version":"2.0"},)";
        gltf += R"("nodes":[{"name":"j0"},{"name":"j1"},)"
                R"({"name":"bodyA","mesh":0,"skin":0},{"name":"bodyB","mesh":0,"skin":1}],)";
        gltf += R"("skins":[{"joints":[0],"inverseBindMatrices":6},)"
                R"({"joints":[1],"inverseBindMatrices":6}],)";
        gltf += R"("meshes":[{"primitives":[{"attributes":{)"
                R"("POSITION":0,"NORMAL":1,"TANGENT":2,"JOINTS_0":3,"WEIGHTS_0":4},"indices":5}]}],)";
        gltf += R"("buffers":[{"byteLength":)" + std::to_string(blob.Data.size())
                + R"(,"uri":"data:application/octet-stream;base64,)" + base64 + R"("}],)";
        gltf += R"("bufferViews":[)"
                + view(pos) + "," + view(nrm) + "," + view(tan) + "," + view(joints) + ","
                + view(weights) + "," + view(idx) + "," + view(ibm) + "],";
        gltf += R"("accessors":[)"
                R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
                R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)"
                R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},)"
                R"({"bufferView":3,"componentType":5123,"count":3,"type":"VEC4"},)"
                R"({"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},)"
                R"({"bufferView":5,"componentType":5123,"count":3,"type":"SCALAR"},)"
                R"({"bufferView":6,"componentType":5126,"count":1,"type":"MAT4"}]})";
        return gltf;
    }
}

TEST(SkeletalCook, RejectsAnimationTargetingMultipleSkins)
{
    // An animation whose channels target joints in two different skins is a
    // multi-character export. The cook refuses it rather than silently
    // dropping the second skin's tracks (no silent partial correctness).
    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(BuildTwoSkinAnimatedGltf()), scene, &error));
    EXPECT_NE(error.find("different skins"), std::string::npos) << error;
}

TEST(SkeletalCook, RejectsMeshInstancedWithMultipleSkins)
{
    // One mesh referenced by two nodes with different skins is ambiguous —
    // the cook cannot pick a single skeleton for one artifact, so it refuses
    // rather than honoring whichever node it scanned first.
    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(BuildMeshInstancedWithTwoSkinsGltf()), scene, &error));
    EXPECT_NE(error.find("different skins"), std::string::npos) << error;
}

TEST(SkeletalCook, ExtractsSkeletonMeshAndAnimation)
{
    const std::string gltf = BuildSkinnedAnimatedGltf();

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;

    // Skeleton: two joints, topologically ordered, child bound at +Y.
    ASSERT_EQ(scene.Skeletons.size(), 1u);
    const SkeletonData& skeleton = scene.Skeletons[0].Data;
    ASSERT_EQ(skeleton.Joints.size(), 2u);
    EXPECT_EQ(skeleton.Joints[0].Name, "root");
    EXPECT_EQ(skeleton.Joints[0].ParentIndex, -1);
    EXPECT_EQ(skeleton.Joints[1].Name, "child");
    EXPECT_EQ(skeleton.Joints[1].ParentIndex, 0);
    EXPECT_FLOAT_EQ(skeleton.Joints[1].BindTranslation.Y, 1.0f);

    // Skinned mesh: skeleton-local joints, weights normalized to 255.
    ASSERT_EQ(scene.Meshes.size(), 1u);
    const ImportedGltfMesh& mesh = scene.Meshes[0];
    EXPECT_EQ(mesh.SkinIndex, 0);
    ASSERT_TRUE(mesh.Skinning.has_value());
    EXPECT_EQ(mesh.Skinning->JointCount, 2u);
    ASSERT_EQ(mesh.Skinning->Influences.size(), 3u);
    for (const MeshSkinInfluence& influence : mesh.Skinning->Influences)
    {
        uint32_t sum = 0;
        for (int slot = 0; slot < 4; ++slot)
        {
            sum += influence.Weights[slot];
            EXPECT_LT(influence.Joints[slot], 2u);
        }
        EXPECT_EQ(sum, 255u);
    }
    // The third vertex is split 0.5/0.5 between joints 0 and 1.
    EXPECT_EQ(mesh.Skinning->Influences[2].Joints[0], 0u);
    EXPECT_EQ(mesh.Skinning->Influences[2].Joints[1], 1u);

    // Animation: one rotation track on the child joint, two keys.
    ASSERT_EQ(scene.Animations.size(), 1u);
    const AnimationClipData& clip = scene.Animations[0].Data;
    ASSERT_EQ(clip.Tracks.size(), 1u);
    EXPECT_EQ(clip.Tracks[0].JointIndex, 1u);
    EXPECT_EQ(clip.Tracks[0].Path, AnimationChannelPath::Rotation);
    EXPECT_EQ(clip.Tracks[0].TimesSeconds.size(), 2u);
    EXPECT_FLOAT_EQ(clip.DurationSeconds, 1.0f);
}

TEST(SkeletalCook, ImporterEmitsThreeArtifactKindsThatRoundTrip)
{
    const std::string gltf = BuildSkinnedAnimatedGltf();

    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result = importer.Import(ImportInput{ "chars/hero.glb", AsBytes(gltf) }, output);
    ASSERT_TRUE(result.IsValid()) << result.Error;

    // One of each artifact kind, all under the source's '#'-suffixed family.
    const CookedArtifact* skeleton = nullptr;
    const CookedArtifact* skinnedMesh = nullptr;
    const CookedArtifact* animation = nullptr;
    for (const CookedArtifact& artifact : result.Artifacts)
    {
        if (artifact.Type == AssetType::Skeleton) skeleton = &artifact;
        else if (artifact.Type == AssetType::SkinnedMesh) skinnedMesh = &artifact;
        else if (artifact.Type == AssetType::AnimationClip) animation = &artifact;
    }
    ASSERT_NE(skeleton, nullptr);
    ASSERT_NE(skinnedMesh, nullptr);
    ASSERT_NE(animation, nullptr);

    EXPECT_TRUE(skeleton->Path.starts_with("asset://chars/hero.glb#"));
    EXPECT_TRUE(skinnedMesh->Path.starts_with("asset://chars/hero.glb#"));
    EXPECT_TRUE(skinnedMesh->FileRelPath.ends_with(".skmesh"));

    // The skeleton artifact round-trips.
    SkeletonData loadedSkeleton;
    std::string error;
    ASSERT_TRUE(LoadSskelFromBytes(output.Files.at(skeleton->FileRelPath), loadedSkeleton, &error)) << error;
    EXPECT_EQ(loadedSkeleton.Joints.size(), 2u);

    // The skinned mesh round-trips through the skinned path and references
    // the skeleton artifact by path.
    LoggingProvider logging;
    MeshLoader meshLoader(logging);
    SkinnedMeshData loadedMesh;
    ASSERT_TRUE(meshLoader.LoadSkinnedFromBytes(output.Files.at(skinnedMesh->FileRelPath), loadedMesh));
    EXPECT_EQ(loadedMesh.Skinning.SkeletonPath, skeleton->Path);

    // The animation round-trips and references the skeleton artifact by path.
    AnimationClipData loadedClip;
    ASSERT_TRUE(LoadSanimFromBytes(output.Files.at(animation->FileRelPath), loadedClip, &error)) << error;
    EXPECT_EQ(loadedClip.SkeletonPath, skeleton->Path);
    EXPECT_EQ(loadedClip.Tracks.size(), 1u);
}

TEST(SkeletalCook, RejectsSkinnedMeshWithoutTangents)
{
    // A skinned primitive with UV and no TANGENT would need MikkTSpace, whose
    // de-index/reweld desyncs the influence stream — so the cook refuses and
    // asks for a tangent re-export (Decision M / N).
    BlobBuilder blob;
    const auto pos = blob.Add<float>({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });
    const auto nrm = blob.Add<float>({ 0, 0, 1, 0, 0, 1, 0, 0, 1 });
    const auto uv = blob.Add<float>({ 0, 0, 1, 0, 0, 1 });
    const auto joints = blob.Add<uint16_t>({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    const auto weights = blob.Add<float>({ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 });
    const auto idx = blob.Add<uint16_t>({ 0, 1, 2 });
    const auto ibm = blob.Add<float>({
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 });
    const std::string base64 = Base64Encode(blob.Data);
    const auto view = [](const BlobBuilder::View& v) {
        return std::string("{\"buffer\":0,\"byteOffset\":") + std::to_string(v.Offset)
            + ",\"byteLength\":" + std::to_string(v.Length) + "}";
    };
    const std::string gltf = std::string(R"({"asset":{"version":"2.0"},)")
        + R"("nodes":[{"name":"j","children":[]},{"name":"body","mesh":0,"skin":0}],)"
        + R"("skins":[{"joints":[0],"inverseBindMatrices":6}],)"
        + R"("meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,)"
          R"("JOINTS_0":3,"WEIGHTS_0":4},"indices":5}]}],)"
        + R"("buffers":[{"byteLength":)" + std::to_string(blob.Data.size())
        + R"(,"uri":"data:application/octet-stream;base64,)" + base64 + R"("}],)"
        + R"("bufferViews":[)" + view(pos) + "," + view(nrm) + "," + view(uv) + "," + view(joints)
        + "," + view(weights) + "," + view(idx) + "," + view(ibm) + "],"
        + R"("accessors":[)"
          R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
          R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)"
          R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},)"
          R"({"bufferView":3,"componentType":5123,"count":3,"type":"VEC4"},)"
          R"({"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},)"
          R"({"bufferView":5,"componentType":5123,"count":3,"type":"SCALAR"},)"
          R"({"bufferView":6,"componentType":5126,"count":1,"type":"MAT4"}]})";

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("TANGENT"), std::string::npos) << error;
}

#endif // SENCHA_ENABLE_COOK
