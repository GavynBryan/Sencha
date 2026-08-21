// Writes the golden skinned-mesh fixture: a two-joint column as .skmesh +
// .sskel under <root>/meshes/dev/. Env-gated like the level cooks -- the
// golden harness runs it before cooking, so the artifacts always match the
// current format version instead of rotting as committed bytes (the
// GenerateCubeDemoAssets rationale).
//
// The shape is two stacked boxes: the lower bound to joint 0, the upper to
// joint 1. Recognizable in a frame, trivial to reason about, and the joints
// have real translations so the palette identity at bind pose is a fact about
// InverseBind, not about everything being at the origin.

#include <gtest/gtest.h>

#include <anim/SkinningPalette.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/logging/LoggingProvider.h>
#include <render/skinned_mesh/SkinnedMeshData.h>
#include <render/static_mesh/MeshValidation.h>
#include <render/static_mesh/StaticMeshPrimitives.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace
{

// Appends BuildCube's unit box, scaled and lifted, with every vertex bound
// fully to one joint.
void AppendBox(SkinnedMeshData& data, float size, float centerY, uint16_t joint)
{
    const MeshGeometry box = StaticMeshPrimitives::BuildCube(size);
    const uint32_t base = static_cast<uint32_t>(data.Geometry.Vertices.size());
    for (StaticMeshVertex vertex : box.Vertices)
    {
        vertex.Position.Y += centerY;
        data.Geometry.Vertices.push_back(vertex);
        data.Skinning.Influences.push_back(
            MeshSkinInfluence{ .Joints = { joint, 0, 0, 0 },
                               .Weights = { 255, 0, 0, 0 } });
    }
    for (const uint32_t index : box.Indices)
        data.Geometry.Indices.push_back(base + index);
    Aabb3d lifted = box.LocalBounds;
    lifted.Min.Y += centerY;
    lifted.Max.Y += centerY;
    data.Geometry.LocalBounds.ExpandToInclude(lifted);
}

bool WriteBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
        return false;
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

} // namespace

TEST(SkinnedFixture, Generate)
{
    const char* root = std::getenv("SENCHA_SKINNED_FIXTURE_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "set SENCHA_SKINNED_FIXTURE_ROOT to write the fixture";

    // Two joints, the upper parented to the lower with a real offset, so the
    // cooked InverseBind matrices are non-trivial.
    SkeletonData skeleton;
    SkeletonJoint lower;
    lower.Name = "lower";
    lower.ParentIndex = -1;
    lower.BindTranslation = Vec3d(0.0f, 0.5f, 0.0f);
    skeleton.Joints.push_back(lower);
    SkeletonJoint upper;
    upper.Name = "upper";
    upper.ParentIndex = 0;
    upper.BindTranslation = Vec3d(0.0f, 1.0f, 0.0f);
    skeleton.Joints.push_back(upper);

    std::vector<Mat4> bindModel;
    BuildBindModelTransforms(skeleton, bindModel);
    for (size_t joint = 0; joint < skeleton.Joints.size(); ++joint)
        skeleton.Joints[joint].InverseBind = bindModel[joint].Inverse();

    std::string error;
    ASSERT_TRUE(ValidateSkeletonData(skeleton, &error)) << error;

    SkinnedMeshData mesh;
    mesh.Skinning.SkeletonPath = "asset://meshes/dev/golden_rig.sskel";
    mesh.Skinning.JointCount = static_cast<uint32_t>(skeleton.Joints.size());
    AppendBox(mesh, 1.0f, 0.5f, 0);
    AppendBox(mesh, 0.6f, 1.5f, 1);

    StaticMeshSection section;
    section.MaterialSlot = 0;
    section.IndexOffset = 0;
    section.IndexCount = static_cast<uint32_t>(mesh.Geometry.Indices.size());
    section.VertexOffset = 0;
    section.VertexCount = static_cast<uint32_t>(mesh.Geometry.Vertices.size());
    section.LocalBounds = mesh.Geometry.LocalBounds;
    mesh.Geometry.Sections.push_back(section);

    const std::filesystem::path directory =
        std::filesystem::path(root) / "meshes" / "dev";
    std::filesystem::create_directories(directory);

    std::vector<std::byte> skeletonBytes;
    ASSERT_TRUE(WriteSskelToBytes(skeleton, skeletonBytes, &error)) << error;
    ASSERT_TRUE(WriteBytes(directory / "golden_rig.sskel", skeletonBytes));

    const MeshValidationResult validation =
        ValidateSkinnedMeshData(mesh);
    for (const MeshValidationError& issue : validation.Errors)
        ADD_FAILURE() << issue.Message;
    ASSERT_TRUE(validation.IsValid());

    LoggingProvider logging;
    MeshSerializer serializer(logging);
    ASSERT_TRUE(serializer.WriteSkinnedToFile(
        (directory / "golden_rig.skmesh").generic_string(), mesh));

    std::printf("skinned fixture: %zu vertices, %u joints -> %s\n",
                mesh.Geometry.Vertices.size(), mesh.Skinning.JointCount,
                directory.generic_string().c_str());
}
