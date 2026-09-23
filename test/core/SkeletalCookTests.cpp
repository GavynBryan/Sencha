// Stage 5 (docs/assets/pipeline.md, Decisions J, M, N): the glTF skin /
// animation cook. Zero-thread, no filesystem — a skinned, animated glTF is
// built in memory (data-URI buffer) and the importer writes to a memory
// output. The cooked artifacts are round-tripped back through the runtime
// loaders so the cook→load chain is exercised end to end.

#include <gtest/gtest.h>

#ifdef SENCHA_ENABLE_COOK

#include <anim/AnimationClipSampling.h>
#include <anim/SkinningPalette.h>
#include <assets/animation/AnimationClipSerializer.h>
#include <assets/cook/GltfFrame.h>
#include <assets/cook/MeshCook.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <assets/static_mesh/MeshLoader.h>
#include <core/logging/LoggingProvider.h>
#include <math/geometry/3d/Transform3d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
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
        // Inverse-bind matrices, column-major: identity for the root, and the
        // inverse of the child's +Y rest offset for the child.
        const auto ibm = blob.Add<float>({
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1 });
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

    // The builder's triangle as a skinned mesh (mesh 0) and, for rigid parts,
    // the same triangle with no skin attributes.
    constexpr std::string_view kSkinnedTrianglePrimitive =
        R"({"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2,"JOINTS_0":3,"WEIGHTS_0":4},"indices":5)";
    constexpr std::string_view kRigidTrianglePrimitive =
        R"({"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2},"indices":5)";
    constexpr std::string_view kSkinnedTriangleMeshes =
        R"([{"name":"body","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TANGENT":2,"JOINTS_0":3,"WEIGHTS_0":4},"indices":5}]}])";

    // A glTF around one skinned triangle bound rigidly to joint 0. Callers
    // write the nodes, skins and animations as JSON and add the float
    // accessors those reference (inverse binds, key times, key values);
    // accessors 0-5 are the triangle's own.
    class SkinnedGltfBuilder
    {
    public:
        SkinnedGltfBuilder()
        {
            AddAccessor(Blob.Add<float>({ 0, 0, 0, 1, 0, 0, 0, 1, 0 }), 5126, 3, "VEC3",
                        R"(,"min":[0,0,0],"max":[1,1,0])");
            AddAccessor(Blob.Add<float>({ 0, 0, 1, 0, 0, 1, 0, 0, 1 }), 5126, 3, "VEC3");
            AddAccessor(Blob.Add<float>({ 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1 }), 5126, 3, "VEC4");
            AddAccessor(Blob.Add<uint16_t>({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }), 5123, 3, "VEC4");
            AddAccessor(Blob.Add<float>({ 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 }), 5126, 3, "VEC4");
            AddAccessor(Blob.Add<uint16_t>({ 0, 1, 2 }), 5123, 3, "SCALAR");
        }

        int AddTimes(const std::vector<float>& times)
        {
            const auto [lo, hi] = std::minmax_element(times.begin(), times.end());
            return AddAccessor(Blob.Add<float>(times), 5126, times.size(), "SCALAR",
                               std::format(R"(,"min":[{}],"max":[{}])", *lo, *hi));
        }

        int AddVec3s(const std::vector<Vec3d>& values)
        {
            std::vector<float> flat;
            for (const Vec3d& v : values)
                flat.insert(flat.end(), { v.X, v.Y, v.Z });
            return AddAccessor(Blob.Add<float>(flat), 5126, values.size(), "VEC3");
        }

        int AddQuats(const std::vector<Quat<float>>& values)
        {
            std::vector<float> flat;
            for (const Quat<float>& q : values)
                flat.insert(flat.end(), { q.X, q.Y, q.Z, q.W });
            return AddAccessor(Blob.Add<float>(flat), 5126, values.size(), "VEC4");
        }

        // Row-major engine matrices, written column-major as glTF stores them.
        int AddMatrices(const std::vector<Mat4>& matrices)
        {
            std::vector<float> flat;
            for (const Mat4& m : matrices)
                for (int col = 0; col < 4; ++col)
                    for (int row = 0; row < 4; ++row)
                        flat.push_back(m.Data[row][col]);
            return AddAccessor(Blob.Add<float>(flat), 5126, matrices.size(), "MAT4");
        }

        // `nodes`, `skins`, `animations`, `meshes` and `materials` are JSON
        // arrays. The default meshes are the skinned triangle alone.
        std::string Build(std::string_view sceneNodes,
                          std::string_view nodes,
                          std::string_view skins,
                          std::string_view animations = {},
                          std::string_view meshes = kSkinnedTriangleMeshes,
                          std::string_view materials = {}) const
        {
            std::string gltf = R"({"asset":{"version":"2.0"},"scene":0,)";
            gltf += std::format(R"("scenes":[{{"nodes":{}}}],"nodes":{},"skins":{},"meshes":{},)",
                                sceneNodes, nodes, skins, meshes);
            if (!animations.empty())
                gltf += std::format(R"("animations":{},)", animations);
            if (!materials.empty())
                gltf += std::format(R"("materials":{},)", materials);
            gltf += R"("buffers":[{"byteLength":)" + std::to_string(Blob.Data.size())
                    + R"(,"uri":"data:application/octet-stream;base64,)" + Base64Encode(Blob.Data)
                    + R"("}],"bufferViews":[)";
            for (std::size_t i = 0; i < Views.size(); ++i)
                gltf += (i == 0 ? "" : ",") + Views[i];
            gltf += R"(],"accessors":[)";
            for (std::size_t i = 0; i < Accessors.size(); ++i)
                gltf += (i == 0 ? "" : ",") + Accessors[i];
            gltf += "]}";
            return gltf;
        }

    private:
        int AddAccessor(const BlobBuilder::View& view, int componentType, std::size_t count,
                        std::string_view type, std::string_view extra = {})
        {
            const int viewIndex = static_cast<int>(Views.size());
            Views.push_back(std::format(R"({{"buffer":0,"byteOffset":{},"byteLength":{}}})",
                                        view.Offset, view.Length));
            Accessors.push_back(std::format(
                R"({{"bufferView":{},"componentType":{},"count":{},"type":"{}"{}}})",
                viewIndex, componentType, count, type, extra));
            return static_cast<int>(Accessors.size()) - 1;
        }

        BlobBuilder Blob;
        std::vector<std::string> Views;
        std::vector<std::string> Accessors;
    };

    Mat4 Trs(const Vec3d& translation, const Quat<float>& rotation, const Vec3d& scale)
    {
        return Transform3f{ translation, rotation, scale }.ToMat4();
    }

    // The armature every model-space fixture hangs its joints from: offset,
    // turned a quarter about X and uniformly scaled, as an exported rig object
    // usually is.
    const Vec3d kArmatureTranslation{ 0.0f, 2.0f, 3.0f };
    const Quat<float> kArmatureRotation =
        Quat<float>::FromAxisAngle(Vec3d(1, 0, 0), std::numbers::pi_v<float> / 2.0f);
    constexpr float kArmatureScale = 2.5f;

    Mat4 ArmatureMatrix()
    {
        return Trs(kArmatureTranslation, kArmatureRotation,
                   Vec3d(kArmatureScale, kArmatureScale, kArmatureScale));
    }

    std::string ArmatureNodeJson(std::string_view children)
    {
        return std::format(
            R"({{"name":"Armature","children":{},"translation":[{},{},{}],)"
            R"("rotation":[{},{},{},{}],"scale":[{},{},{}]}})",
            children, kArmatureTranslation.X, kArmatureTranslation.Y, kArmatureTranslation.Z,
            kArmatureRotation.X, kArmatureRotation.Y, kArmatureRotation.Z, kArmatureRotation.W,
            kArmatureScale, kArmatureScale, kArmatureScale);
    }

    // A scene-space transform as the cooked skeleton holds it: model space is
    // the engine frame.
    Mat4 InEngineFrame(const Mat4& sceneTransform)
    {
        return GltfToEngineMatrix() * sceneTransform;
    }

    void ExpectMatrixNear(const Mat4& actual, const Mat4& expected, float tolerance,
                          std::string_view label)
    {
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col)
                EXPECT_NEAR(actual.Data[row][col], expected.Data[row][col], tolerance)
                    << label << " [" << row << "][" << col << "]";
    }

    // The model transforms a clip poses at `time`, the way the runtime
    // composes them.
    std::vector<Mat4> PosedModel(const ImportedGltfScene& scene, float time)
    {
        std::vector<Transform3f> local;
        SampleAnimationClip(scene.Animations.at(0).Data, scene.Skeletons.at(0).Data, time, local);
        std::vector<Mat4> model;
        BuildPosedModelTransforms(scene.Skeletons.at(0).Data, local, model);
        return model;
    }

    const AnimationJointTrack* FindTrack(const AnimationClipData& clip, uint32_t joint,
                                         AnimationChannelPath path)
    {
        for (const AnimationJointTrack& track : clip.Tracks)
            if (track.JointIndex == joint && track.Path == path)
                return &track;
        return nullptr;
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

TEST(SkeletalCook, MeshInstancedWithTwoSkinsJoinsBothModels)
{
    // Each skin's model holds what that skin draws, so a mesh placed with two
    // skins is part of both.
    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(BuildMeshInstancedWithTwoSkinsGltf()), scene, &error)) << error;
    ASSERT_EQ(scene.SkinnedModels.size(), 2u);
    EXPECT_EQ(scene.SkinnedModels[0].SkinIndex, 0);
    EXPECT_EQ(scene.SkinnedModels[1].SkinIndex, 1);
    EXPECT_EQ(scene.SkinnedModels[0].Geometry.Vertices.size(), 3u);
    EXPECT_EQ(scene.SkinnedModels[1].Geometry.Vertices.size(), 3u);
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

    // The skeleton's model: skeleton-local joints, weights normalized to 255.
    ASSERT_EQ(scene.SkinnedModels.size(), 1u);
    EXPECT_TRUE(scene.StaticMeshes.empty());
    const ImportedSkinnedModel& model = scene.SkinnedModels[0];
    EXPECT_EQ(model.SkinIndex, 0);
    EXPECT_EQ(model.Name, "rig");
    EXPECT_EQ(model.Skinning.JointCount, 2u);
    ASSERT_EQ(model.Skinning.Influences.size(), 3u);
    for (const MeshSkinInfluence& influence : model.Skinning.Influences)
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
    EXPECT_EQ(model.Skinning.Influences[2].Joints[0], 0u);
    EXPECT_EQ(model.Skinning.Influences[2].Joints[1], 1u);

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

    EXPECT_EQ(skeleton->Path, "asset://chars/hero.glb#skel:rig");
    EXPECT_EQ(skinnedMesh->Path, "asset://chars/hero.glb#model:rig");
    EXPECT_EQ(skinnedMesh->FileRelPath, ".cooked/chars/hero.glb.model:rig.skmesh");
    EXPECT_EQ(animation->Path, "asset://chars/hero.glb#anim:wave");
    EXPECT_EQ(result.Artifacts.size(), 3u);

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

// -- Skeleton model space ------------------------------------------------------
//
// A glTF skin's inverse bind matrices are written in scene space, which
// includes every non-joint node above the root joints (an exported armature
// object, for one). The cook folds those nodes and the engine-frame turn into
// the roots, and the nodes' animation into the roots' tracks.

namespace
{
    // Armature(0) -> Root(1) -> Upper(2), plus Body(3) skinned to [Root, Upper],
    // with inverse binds that agree with the rest pose.
    std::string BuildArmatureRig(SkinnedGltfBuilder& builder, std::string_view animations = {})
    {
        const Mat4 root = ArmatureMatrix() * Mat4::MakeTranslation(0, 1, 0);
        const Mat4 upper = root * Mat4::MakeTranslation(0, 1, 0);
        const int ibm = builder.AddMatrices({ root.Inverse(), upper.Inverse() });
        return builder.Build(
            "[0,3]",
            "[" + ArmatureNodeJson("[1]")
                + R"(,{"name":"Root","children":[2],"translation":[0,1,0]},)"
                  R"({"name":"Upper","translation":[0,1,0]},{"name":"Body","mesh":0,"skin":0}])",
            std::format(R"([{{"name":"rig","joints":[1,2],"inverseBindMatrices":{}}}])", ibm),
            animations);
    }

    // Mover(0) -> Armature(1) -> {Root(2) -> Upper(3), Target(4)}, Body(5).
    // Two root joints share the armature, the way a rig's IK targets do.
    std::string BuildMovedRig(SkinnedGltfBuilder& builder, std::string_view animations)
    {
        const Mat4 root = ArmatureMatrix() * Mat4::MakeTranslation(0, 1, 0);
        const Mat4 upper = root * Mat4::MakeTranslation(0, 1, 0);
        const Mat4 target = ArmatureMatrix() * Mat4::MakeTranslation(1, 0, 0);
        const int ibm = builder.AddMatrices({ root.Inverse(), upper.Inverse(), target.Inverse() });
        return builder.Build(
            "[0,5]",
            R"([{"name":"Mover","children":[1]},)" + ArmatureNodeJson("[2,4]")
                + R"(,{"name":"Root","children":[3],"translation":[0,1,0]},)"
                  R"({"name":"Upper","translation":[0,1,0]},)"
                  R"({"name":"Target","translation":[1,0,0]},)"
                  R"({"name":"Body","mesh":0,"skin":0}])",
            std::format(R"([{{"name":"rig","joints":[2,3,4],"inverseBindMatrices":{}}}])", ibm),
            animations);
    }

    Vec3d Lerp(const Vec3d& a, const Vec3d& b, float t)
    {
        return a + (b - a) * t;
    }

    const Quat<float> kQuarterTurnZ =
        Quat<float>::FromAxisAngle(Vec3d(0, 0, 1), std::numbers::pi_v<float> / 2.0f);
}

TEST(SkeletalCook, RestPaletteIsIdentityUnderATransformedArmature)
{
    SkinnedGltfBuilder builder;
    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(BuildArmatureRig(builder)), scene, &error)) << error;

    const SkeletonData& skeleton = scene.Skeletons.at(0).Data;
    std::vector<Mat4> model;
    std::vector<Mat4> palette;
    BuildBindModelTransforms(skeleton, model);
    BuildSkinningPalette(skeleton, model, palette);

    const Mat4 root = ArmatureMatrix() * Mat4::MakeTranslation(0, 1, 0);
    ExpectMatrixNear(model.at(0), InEngineFrame(root), 1e-4f, "Root model");
    ExpectMatrixNear(model.at(1), InEngineFrame(root * Mat4::MakeTranslation(0, 1, 0)), 1e-4f,
                     "Upper model");
    for (std::size_t joint = 0; joint < palette.size(); ++joint)
        ExpectMatrixNear(palette[joint], Mat4::Identity(), 1e-4f, std::format("palette {}", joint));
}

TEST(SkeletalCook, RootTracksComposeAStaticArmature)
{
    SkinnedGltfBuilder builder;
    const int times = builder.AddTimes({ 0.0f, 1.0f });
    const int translations = builder.AddVec3s({ Vec3d(0, 1, 0), Vec3d(1, 1, 0) });
    const int rotations = builder.AddQuats({ Quat<float>::Identity(), kQuarterTurnZ });
    const std::string animations = std::format(
        R"([{{"name":"sway","channels":[)"
        R"({{"sampler":0,"target":{{"node":1,"path":"translation"}}}},)"
        R"({{"sampler":1,"target":{{"node":1,"path":"rotation"}}}}],)"
        R"("samplers":[{{"input":{0},"output":{1}}},{{"input":{0},"output":{2}}}]}}])",
        times, translations, rotations);

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(BuildArmatureRig(builder, animations)), scene, &error))
        << error;
    ASSERT_EQ(scene.Animations.size(), 1u);

    // A static armature composes key by key: the key times are the source's.
    const AnimationClipData& clip = scene.Animations[0].Data;
    for (const AnimationJointTrack& track : clip.Tracks)
        EXPECT_EQ(track.TimesSeconds, (std::vector<float>{ 0.0f, 1.0f }));

    for (const float time : { 0.0f, 0.5f, 1.0f })
    {
        const Mat4 local = Trs(Lerp(Vec3d(0, 1, 0), Vec3d(1, 1, 0), time),
                               Quat<float>::Slerp(Quat<float>::Identity(), kQuarterTurnZ, time),
                               Vec3d(1, 1, 1));
        const std::vector<Mat4> model = PosedModel(scene, time);
        ExpectMatrixNear(model.at(0), InEngineFrame(ArmatureMatrix() * local), 1e-4f,
                         std::format("Root at {}s", time));
    }
}

TEST(SkeletalCook, AnimatedAncestorSynthesizesRootTracks)
{
    // Only the object above the armature moves; the joints carry no keys.
    SkinnedGltfBuilder builder;
    const std::vector<Vec3d> mover{ Vec3d(0, 0, 0), Vec3d(4, 0, 0), Vec3d(4, 0, -3) };
    const int times = builder.AddTimes({ 0.0f, 1.0f, 2.0f });
    const int translations = builder.AddVec3s(mover);
    const std::string animations = std::format(
        R"([{{"name":"travel","channels":[{{"sampler":0,"target":{{"node":0,"path":"translation"}}}}],)"
        R"("samplers":[{{"input":{},"output":{}}}]}}])",
        times, translations);

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(BuildMovedRig(builder, animations)), scene, &error))
        << error;
    ASSERT_EQ(scene.Animations.size(), 1u);

    // Both root joints get the motion; the child joint needs none of its own.
    const AnimationClipData& clip = scene.Animations[0].Data;
    for (const uint32_t root : { 0u, 2u })
        for (const AnimationChannelPath path : { AnimationChannelPath::Translation,
                                                 AnimationChannelPath::Rotation,
                                                 AnimationChannelPath::Scale })
        {
            const AnimationJointTrack* track = FindTrack(clip, root, path);
            ASSERT_NE(track, nullptr) << "joint " << root;
            EXPECT_EQ(track->TimesSeconds, (std::vector<float>{ 0.0f, 1.0f, 2.0f }));
        }
    EXPECT_EQ(FindTrack(clip, 1, AnimationChannelPath::Translation), nullptr);

    for (const float time : { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f })
    {
        const Vec3d offset = time <= 1.0f ? Lerp(mover[0], mover[1], time)
                                          : Lerp(mover[1], mover[2], time - 1.0f);
        const Mat4 frame = InEngineFrame(Mat4::MakeTranslation(offset) * ArmatureMatrix());
        const std::vector<Mat4> model = PosedModel(scene, time);
        ExpectMatrixNear(model.at(0), frame * Mat4::MakeTranslation(0, 1, 0), 1e-4f,
                         std::format("Root at {}s", time));
        ExpectMatrixNear(model.at(1), frame * Mat4::MakeTranslation(0, 2, 0), 1e-4f,
                         std::format("Upper at {}s", time));
        ExpectMatrixNear(model.at(2), frame * Mat4::MakeTranslation(1, 0, 0), 1e-4f,
                         std::format("Target at {}s", time));
    }
}

TEST(SkeletalCook, RootTracksResampleAtTheUnionOfAncestorAndRootKeys)
{
    SkinnedGltfBuilder builder;
    const int moverTimes = builder.AddTimes({ 0.0f, 1.0f });
    const int moverValues = builder.AddVec3s({ Vec3d(0, 0, 0), Vec3d(4, 0, 0) });
    const int rootTimes = builder.AddTimes({ 0.5f, 2.0f });
    const int rootValues = builder.AddQuats({ Quat<float>::Identity(), kQuarterTurnZ });
    const std::string animations = std::format(
        R"([{{"name":"mixed","channels":[)"
        R"({{"sampler":0,"target":{{"node":0,"path":"translation"}}}},)"
        R"({{"sampler":1,"target":{{"node":2,"path":"rotation"}}}}],)"
        R"("samplers":[{{"input":{},"output":{}}},{{"input":{},"output":{}}}]}}])",
        moverTimes, moverValues, rootTimes, rootValues);

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(BuildMovedRig(builder, animations)), scene, &error))
        << error;
    ASSERT_EQ(scene.Animations.size(), 1u);

    const std::vector<float> unionTimes{ 0.0f, 0.5f, 1.0f, 2.0f };
    const AnimationClipData& clip = scene.Animations[0].Data;
    for (const AnimationChannelPath path : { AnimationChannelPath::Translation,
                                             AnimationChannelPath::Rotation,
                                             AnimationChannelPath::Scale })
    {
        const AnimationJointTrack* track = FindTrack(clip, 0, path);
        ASSERT_NE(track, nullptr);
        EXPECT_EQ(track->TimesSeconds, unionTimes);
    }

    for (const float time : unionTimes)
    {
        // Each source clamps outside its own keys.
        const Vec3d offset = Lerp(Vec3d(0, 0, 0), Vec3d(4, 0, 0), std::min(time, 1.0f));
        const float rootAlpha = std::clamp((time - 0.5f) / 1.5f, 0.0f, 1.0f);
        const Mat4 rootLocal = Trs(Vec3d(0, 1, 0),
                                   Quat<float>::Slerp(Quat<float>::Identity(), kQuarterTurnZ, rootAlpha),
                                   Vec3d(1, 1, 1));
        const std::vector<Mat4> model = PosedModel(scene, time);
        ExpectMatrixNear(model.at(0), InEngineFrame(Mat4::MakeTranslation(offset) * ArmatureMatrix() * rootLocal),
                         1e-4f, std::format("Root at {}s", time));
    }
}

TEST(SkeletalCook, RejectsANonUniformlyScaledAncestor)
{
    SkinnedGltfBuilder builder;
    const int ibm = builder.AddMatrices({ Mat4::Identity() });
    const std::string gltf = builder.Build(
        "[0,2]",
        R"([{"name":"Armature","children":[1],"scale":[1,2,1]},{"name":"Root"},)"
        R"({"name":"Body","mesh":0,"skin":0}])",
        std::format(R"([{{"name":"rig","joints":[1],"inverseBindMatrices":{}}}])", ibm));

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("'Armature'"), std::string::npos) << error;
    EXPECT_NE(error.find("uniform"), std::string::npos) << error;
}

TEST(SkeletalCook, RejectsABindPoseThatIsNotTheRestPose)
{
    // Identity inverse binds against a rest pose that is offset and turned:
    // the source was bound in a pose other than the one its nodes describe.
    SkinnedGltfBuilder builder;
    const int ibm = builder.AddMatrices({ Mat4::Identity(), Mat4::Identity() });
    const std::string gltf = builder.Build(
        "[0,3]",
        "[" + ArmatureNodeJson("[1]")
            + R"(,{"name":"Root","children":[2],"translation":[0,1,0]},)"
              R"({"name":"Upper","translation":[0,1,0]},{"name":"Body","mesh":0,"skin":0}])",
        std::format(R"([{{"name":"rig","joints":[1,2],"inverseBindMatrices":{}}}])", ibm));

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("'Root'"), std::string::npos) << error;
    EXPECT_NE(error.find("rest pose"), std::string::npos) << error;
}

TEST(SkeletalCook, RejectsANonJointNodeBetweenJoints)
{
    SkinnedGltfBuilder builder;
    const int ibm = builder.AddMatrices({ Mat4::Identity(), Mat4::MakeTranslation(0, -2, 0) });
    const std::string gltf = builder.Build(
        "[0,3]",
        R"([{"name":"Root","children":[1]},{"name":"Spacer","children":[2],"translation":[0,1,0]},)"
        R"({"name":"Upper","translation":[0,1,0]},{"name":"Body","mesh":0,"skin":0}])",
        std::format(R"([{{"name":"rig","joints":[0,2],"inverseBindMatrices":{}}}])", ibm));

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("'Spacer'"), std::string::npos) << error;
    EXPECT_NE(error.find("'Upper'"), std::string::npos) << error;
}

// -- One model per skeleton -----------------------------------------------------

namespace
{
    // Armature(0) -> Root(1) -> Upper(2) -> ..., Body(3) skinned to [Root,
    // Upper]. `underUpper` is Upper's child list, `extraNodes` are appended
    // from index 4, and mesh 1 is the rigid triangle.
    std::string BuildRigWithParts(SkinnedGltfBuilder& builder,
                                  std::string_view underUpper,
                                  std::string_view extraNodes,
                                  std::string_view materials = {},
                                  std::string_view bodyMaterial = {},
                                  std::string_view partMaterial = {},
                                  std::string_view animations = {})
    {
        const Mat4 root = ArmatureMatrix() * Mat4::MakeTranslation(0, 1, 0);
        const Mat4 upper = root * Mat4::MakeTranslation(0, 1, 0);
        const int ibm = builder.AddMatrices({ root.Inverse(), upper.Inverse() });
        const std::string meshes = std::format(
            R"([{{"name":"body","primitives":[{}{}}}]}},{{"name":"part","primitives":[{}{}}}]}}])",
            kSkinnedTrianglePrimitive, bodyMaterial, kRigidTrianglePrimitive, partMaterial);
        return builder.Build(
            "[0,3]",
            "[" + ArmatureNodeJson("[1]")
                + R"(,{"name":"Root","children":[2],"translation":[0,1,0]},)"
                + std::format(R"({{"name":"Upper","children":{},"translation":[0,1,0]}},)", underUpper)
                + R"({"name":"Body","mesh":0,"skin":0},)" + std::string(extraNodes) + "]",
            std::format(R"([{{"name":"rig","joints":[1,2],"inverseBindMatrices":{}}}])", ibm),
            animations, meshes, materials);
    }

    const Mat4 kUpperWorld = ArmatureMatrix() * Mat4::MakeTranslation(0, 2, 0);

    // Positions of vertices [first, first + 3) against the builder's triangle
    // carried by `toModel`.
    void ExpectTriangleAt(const MeshGeometry& geometry, std::size_t first, const Mat4& toModel)
    {
        const Vec3d triangle[3]{ Vec3d(0, 0, 0), Vec3d(1, 0, 0), Vec3d(0, 1, 0) };
        for (std::size_t i = 0; i < 3; ++i)
        {
            const Vec4 expected = toModel * Vec4(triangle[i].X, triangle[i].Y, triangle[i].Z, 1.0f);
            const Vec3d& actual = geometry.Vertices.at(first + i).Position;
            EXPECT_NEAR(actual.X, expected.X, 1e-4f) << "vertex " << first + i;
            EXPECT_NEAR(actual.Y, expected.Y, 1e-4f) << "vertex " << first + i;
            EXPECT_NEAR(actual.Z, expected.Z, 1e-4f) << "vertex " << first + i;
        }
    }
}

// A mesh parented to a bone is part of that skeleton's model, bound wholly to
// the bone, in the model's space at rest.
TEST(SkeletalCook, BoneChildJoinsTheModelAsARigidPart)
{
    SkinnedGltfBuilder builder;
    const std::string gltf = BuildRigWithParts(
        builder, "[4]", R"({"name":"Nose","mesh":1,"translation":[0,0.5,0.25]})",
        R"([{"name":"skin"},{"name":"horn"}])", R"(,"material":0)", R"(,"material":1)");

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    EXPECT_TRUE(scene.StaticMeshes.empty());
    ASSERT_EQ(scene.SkinnedModels.size(), 1u);
    const ImportedSkinnedModel& model = scene.SkinnedModels[0];

    ASSERT_EQ(model.Geometry.Sections.size(), 2u);
    const StaticMeshSection& part = model.Geometry.Sections[1];
    EXPECT_EQ(part.MaterialSlot, 1u);
    ASSERT_EQ(part.VertexCount, 3u);
    for (uint32_t v = part.VertexOffset; v < part.VertexOffset + part.VertexCount; ++v)
    {
        const MeshSkinInfluence& influence = model.Skinning.Influences.at(v);
        EXPECT_EQ(influence.Joints[0], 1u); // Upper
        EXPECT_EQ(influence.Weights[0], 255u);
        for (int slot = 1; slot < 4; ++slot)
        {
            EXPECT_EQ(influence.Joints[slot], 0u);
            EXPECT_EQ(influence.Weights[slot], 0u);
        }
    }
    ExpectTriangleAt(model.Geometry, part.VertexOffset,
                     InEngineFrame(kUpperWorld * Mat4::MakeTranslation(0, 0.5f, 0.25f)));
}

// Nodes between the joint and the mesh carry it too: the part is baked
// through every transform from beneath the joint down to the mesh.
TEST(SkeletalCook, RigidPartBakesThroughNonJointNodes)
{
    const Quat<float> tilt = Quat<float>::FromAxisAngle(Vec3d(0, 0, 1), std::numbers::pi_v<float> / 2.0f);
    SkinnedGltfBuilder builder;
    const std::string gltf = BuildRigWithParts(
        builder, "[4]",
        std::format(R"({{"name":"Mount","children":[5],"translation":[1,0,0],"rotation":[{},{},{},{}]}},)"
                    R"({{"name":"Nose","mesh":1,"translation":[0,0.5,0],"scale":[2,2,2]}})",
                    tilt.X, tilt.Y, tilt.Z, tilt.W));

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    ASSERT_EQ(scene.SkinnedModels.size(), 1u);
    const MeshGeometry& geometry = scene.SkinnedModels[0].Geometry;
    ASSERT_EQ(geometry.Sections.size(), 1u); // one material (none) for body and part
    ASSERT_EQ(geometry.Vertices.size(), 6u);

    const Mat4 mount = Trs(Vec3d(1, 0, 0), tilt, Vec3d(1, 1, 1));
    const Mat4 nose = Trs(Vec3d(0, 0.5f, 0), Quat<float>::Identity(), Vec3d(2, 2, 2));
    ExpectTriangleAt(geometry, 3, InEngineFrame(kUpperWorld * mount * nose));
    for (std::size_t v = 3; v < 6; ++v)
        EXPECT_EQ(scene.SkinnedModels[0].Skinning.Influences[v].Joints[0], 1u);
}

TEST(SkeletalCook, AnimatedNodeCarryingARigidPartIsRejected)
{
    SkinnedGltfBuilder builder;
    const int times = builder.AddTimes({ 0.0f, 1.0f });
    const int values = builder.AddVec3s({ Vec3d(1, 0, 0), Vec3d(2, 0, 0) });
    const std::string animations = std::format(
        R"([{{"name":"wiggle","channels":[{{"sampler":0,"target":{{"node":4,"path":"translation"}}}}],)"
        R"("samplers":[{{"input":{},"output":{}}}]}}])",
        times, values);
    const std::string gltf = BuildRigWithParts(
        builder, "[4]",
        R"({"name":"Mount","children":[5],"translation":[1,0,0]},{"name":"Nose","mesh":1})",
        {}, {}, {}, animations);

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("'wiggle'"), std::string::npos) << error;
    EXPECT_NE(error.find("'Mount'"), std::string::npos) << error;
}

// A skinned mesh placed twice with one skin is the same geometry twice: the
// placing node does not move skinned vertices. That is the one duplicate the
// cook folds.
TEST(SkeletalCook, MeshPlacedTwiceWithOneSkinIsOneCopy)
{
    SkinnedGltfBuilder builder;
    const std::string gltf = BuildRigWithParts(
        builder, "[]", R"({"name":"BodyAgain","mesh":0,"skin":0,"translation":[5,0,0]},{"name":"Spare","mesh":1})");

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    ASSERT_EQ(scene.SkinnedModels.size(), 1u);
    EXPECT_EQ(scene.SkinnedModels[0].Geometry.Vertices.size(), 3u);
    ASSERT_EQ(scene.StaticMeshes.size(), 1u); // the unparented triangle stays static
    EXPECT_EQ(scene.StaticMeshes[0].Name, "Spare");
}

TEST(SkeletalCook, ModelSectionsAreOnePerMaterialInFirstAppearanceOrder)
{
    SkinnedGltfBuilder builder;
    const std::string gltf = BuildRigWithParts(
        builder, "[4,5]",
        R"({"name":"Horn","mesh":1},{"name":"Spike","mesh":1,"translation":[0,1,0]})",
        R"([{"name":"skin"}])", R"(,"material":0)", R"(,"material":0)");

    ImportedGltfScene scene;
    std::string error;
    ASSERT_TRUE(ImportGltfScene(AsBytes(gltf), scene, &error)) << error;
    const MeshGeometry& geometry = scene.SkinnedModels.at(0).Geometry;
    ASSERT_EQ(geometry.Sections.size(), 1u);
    EXPECT_EQ(geometry.Sections[0].VertexCount, 9u);
    EXPECT_EQ(geometry.Sections[0].IndexCount, 9u);
}

TEST(SkeletalCook, ModelWithTooManyMaterialsIsRejected)
{
    std::string primitives;
    std::string materials;
    for (int i = 0; i < 33; ++i)
    {
        primitives += std::format("{}{},\"material\":{}}}", i == 0 ? "" : ",", kSkinnedTrianglePrimitive, i);
        materials += std::format(R"({}{{"name":"m{}"}})", i == 0 ? "" : ",", i);
    }
    SkinnedGltfBuilder builder;
    const int ibm = builder.AddMatrices({ Mat4::Identity() });
    const std::string gltf = builder.Build(
        "[0,1]", R"([{"name":"Root"},{"name":"Body","mesh":0,"skin":0}])",
        std::format(R"([{{"name":"rig","joints":[0],"inverseBindMatrices":{}}}])", ibm), {},
        std::format(R"([{{"name":"body","primitives":[{}]}}])", primitives), "[" + materials + "]");

    ImportedGltfScene scene;
    std::string error;
    EXPECT_FALSE(ImportGltfScene(AsBytes(gltf), scene, &error));
    EXPECT_NE(error.find("33 materials"), std::string::npos) << error;
}

TEST(SkeletalCook, DuplicateSkinNamesAreRejected)
{
    SkinnedGltfBuilder builder;
    const int ibm = builder.AddMatrices({ Mat4::Identity() });
    const std::string gltf = builder.Build(
        "[0,1,2]", R"([{"name":"A"},{"name":"B"},{"name":"Body","mesh":0,"skin":0}])",
        std::format(R"([{{"name":"rig","joints":[0],"inverseBindMatrices":{0}}},)"
                    R"({{"name":"rig","joints":[1],"inverseBindMatrices":{0}}}])", ibm));

    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result = importer.Import(ImportInput{ "chars/pair.glb", AsBytes(gltf) }, output);
    EXPECT_FALSE(result.IsValid());
    EXPECT_NE(result.Error.find("skin 0 'rig'"), std::string::npos) << result.Error;
    EXPECT_NE(result.Error.find("skin 1 'rig'"), std::string::npos) << result.Error;
}

TEST(SkeletalCook, UnnamedSkinIsNamedByItsIndex)
{
    SkinnedGltfBuilder builder;
    const int ibm = builder.AddMatrices({ Mat4::Identity() });
    const std::string gltf = builder.Build(
        "[0,1]", R"([{"name":"Root"},{"name":"Body","mesh":0,"skin":0}])",
        std::format(R"([{{"joints":[0],"inverseBindMatrices":{}}}])", ibm));

    GltfMeshImporter importer;
    MemoryCookOutputWriter output;
    const ImportResult result = importer.Import(ImportInput{ "chars/solo.glb", AsBytes(gltf) }, output);
    ASSERT_TRUE(result.IsValid()) << result.Error;
    ASSERT_EQ(result.Artifacts.size(), 2u);
    EXPECT_EQ(result.Artifacts[0].Path, "asset://chars/solo.glb#skel:skin0");
    EXPECT_EQ(result.Artifacts[1].Path, "asset://chars/solo.glb#model:skin0");
}

#endif // SENCHA_ENABLE_COOK
