#include <anim/AnimationClip.h>
#include <anim/AnimationClipCache.h>
#include <anim/Skeleton.h>
#include <anim/SkeletonCache.h>
#include <assets/animation/AnimationClipSerializer.h>
#include <assets/skeleton/SkeletonSerializer.h>
#include <assets/static_mesh/MeshLoader.h>
#include <assets/static_mesh/MeshSerializer.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/MeshGeometry.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
    SkeletonData TwoJointSkeleton()
    {
        SkeletonData skeleton;
        SkeletonJoint root;
        root.Name = "root";
        root.ParentIndex = -1;
        for (int d = 0; d < 4; ++d)
            root.InverseBind.Data[d][d] = 1.0f;

        SkeletonJoint child;
        child.Name = "child";
        child.ParentIndex = 0;
        child.BindTranslation = Vec3d(0, 1, 0);
        for (int d = 0; d < 4; ++d)
            child.InverseBind.Data[d][d] = 1.0f;
        child.InverseBind.Data[1][3] = -1.0f; // inverse of the +Y bind

        skeleton.Joints = { root, child };
        return skeleton;
    }

    AnimationClipData TwoKeyRotationClip(std::string skeletonPath = "asset://chars/hero.glb#skel:rig")
    {
        AnimationJointTrack track;
        track.JointIndex = 1;
        track.Path = AnimationChannelPath::Rotation;
        track.Interpolation = AnimationInterpolation::Linear;
        track.TimesSeconds = { 0.0f, 1.0f };
        track.Values = {
            0.0f, 0.0f, 0.0f, 1.0f, // identity
            0.0f, 0.0f, 0.707106781f, 0.707106781f, // 90° about Z
        };

        AnimationClipData clip;
        clip.SkeletonPath = std::move(skeletonPath);
        clip.DurationSeconds = 1.0f;
        clip.Tracks = { track };
        return clip;
    }

    // A minimal skinned mesh: one triangle, every vertex weighted to joint 0.
    SkinnedMeshData SkinnedTriangle(std::string skeletonPath = "asset://chars/hero.glb#skel:rig")
    {
        SkinnedMeshData mesh;
        for (int i = 0; i < 3; ++i)
        {
            StaticMeshVertex v;
            v.Position = Vec3d(i == 1 ? 1.0 : 0.0, i == 2 ? 1.0 : 0.0, 0.0);
            v.Normal = Vec3d(0, 0, 1);
            v.Tangent = Vec4(1, 0, 0, 1);
            mesh.Geometry.Vertices.push_back(v);
        }
        mesh.Geometry.Indices = { 0, 1, 2 };

        StaticMeshSection section;
        section.IndexOffset = 0;
        section.IndexCount = 3;
        section.VertexOffset = 0;
        section.VertexCount = 3;
        mesh.Geometry.Sections = { section };

        mesh.Skinning.SkeletonPath = std::move(skeletonPath);
        mesh.Skinning.JointCount = 2;
        for (int i = 0; i < 3; ++i)
        {
            MeshSkinInfluence influence{};
            influence.Joints[0] = 0;
            influence.Weights[0] = 255;
            mesh.Skinning.Influences.push_back(influence);
        }
        return mesh;
    }
}

// -- Skeleton format ----------------------------------------------------------

TEST(SkeletonSerializer, RoundTrips)
{
    const SkeletonData skeleton = TwoJointSkeleton();

    std::vector<std::byte> bytes;
    std::string error;
    ASSERT_TRUE(WriteSskelToBytes(skeleton, bytes, &error)) << error;

    SkeletonData loaded;
    ASSERT_TRUE(LoadSskelFromBytes(bytes, loaded, &error)) << error;
    ASSERT_EQ(loaded.Joints.size(), 2u);
    EXPECT_EQ(loaded.Joints[0].Name, "root");
    EXPECT_EQ(loaded.Joints[1].Name, "child");
    EXPECT_EQ(loaded.Joints[1].ParentIndex, 0);
    EXPECT_FLOAT_EQ(loaded.Joints[1].BindTranslation.Y, 1.0f);
    EXPECT_FLOAT_EQ(loaded.Joints[1].InverseBind.Data[1][3], -1.0f);
}

TEST(SkeletonSerializer, ValidationRejectsBadHierarchyAndCounts)
{
    std::string error;

    SkeletonData empty;
    EXPECT_FALSE(ValidateSkeletonData(empty, &error));

    // A child before its parent breaks the topological-order invariant.
    SkeletonData outOfOrder;
    SkeletonJoint child;
    child.ParentIndex = 1; // forward reference
    for (int d = 0; d < 4; ++d) child.InverseBind.Data[d][d] = 1.0f;
    SkeletonJoint root;
    root.ParentIndex = -1;
    for (int d = 0; d < 4; ++d) root.InverseBind.Data[d][d] = 1.0f;
    outOfOrder.Joints = { child, root };
    EXPECT_FALSE(ValidateSkeletonData(outOfOrder, &error));

    // Non-unit bind rotation.
    SkeletonData badRotation = TwoJointSkeleton();
    badRotation.Joints[0].BindRotation = Quat<float>(0.0f, 0.0f, 0.0f, 0.5f);
    EXPECT_FALSE(ValidateSkeletonData(badRotation, &error));
}

TEST(SkeletonSerializer, WriterRejectsWhatReaderWouldReject)
{
    // The writer must refuse to produce an artifact the reader would refuse —
    // here, a joint name longer than the reader's cap (4096).
    SkeletonData skeleton = TwoJointSkeleton();
    skeleton.Joints[1].Name = std::string(5000, 'j');

    std::vector<std::byte> bytes;
    std::string error;
    EXPECT_FALSE(WriteSskelToBytes(skeleton, bytes, &error));
    EXPECT_FALSE(error.empty());
}

TEST(SkeletonSerializer, RejectsBadMagicAndVersion)
{
    SkeletonData loaded;
    std::vector<std::byte> garbage{ std::byte{'X'}, std::byte{'X'}, std::byte{'X'}, std::byte{'X'} };
    EXPECT_FALSE(LoadSskelFromBytes(garbage, loaded));

    std::vector<std::byte> bytes;
    ASSERT_TRUE(WriteSskelToBytes(TwoJointSkeleton(), bytes));
    bytes[4] = std::byte{ 99 }; // corrupt the version field
    EXPECT_FALSE(LoadSskelFromBytes(bytes, loaded));
}

// -- Animation clip format ----------------------------------------------------

TEST(AnimationClipSerializer, RoundTrips)
{
    const AnimationClipData clip = TwoKeyRotationClip();

    std::vector<std::byte> bytes;
    std::string error;
    ASSERT_TRUE(WriteSanimToBytes(clip, bytes, &error)) << error;

    AnimationClipData loaded;
    ASSERT_TRUE(LoadSanimFromBytes(bytes, loaded, &error)) << error;
    EXPECT_EQ(loaded.SkeletonPath, clip.SkeletonPath);
    EXPECT_FLOAT_EQ(loaded.DurationSeconds, 1.0f);
    ASSERT_EQ(loaded.Tracks.size(), 1u);
    EXPECT_EQ(loaded.Tracks[0].JointIndex, 1u);
    EXPECT_EQ(loaded.Tracks[0].Path, AnimationChannelPath::Rotation);
    EXPECT_EQ(loaded.Tracks[0].TimesSeconds.size(), 2u);
    EXPECT_EQ(loaded.Tracks[0].Values.size(), 8u);
}

TEST(AnimationClipSerializer, ValidationRejectsBadTracks)
{
    std::string error;

    AnimationClipData noTracks = TwoKeyRotationClip();
    noTracks.Tracks.clear();
    EXPECT_FALSE(ValidateAnimationClipData(noTracks, &error));

    AnimationClipData nonAscending = TwoKeyRotationClip();
    nonAscending.Tracks[0].TimesSeconds = { 1.0f, 0.0f };
    EXPECT_FALSE(ValidateAnimationClipData(nonAscending, &error));

    AnimationClipData wrongValueCount = TwoKeyRotationClip();
    wrongValueCount.Tracks[0].Values.pop_back();
    EXPECT_FALSE(ValidateAnimationClipData(wrongValueCount, &error));

    AnimationClipData nonUnitQuat = TwoKeyRotationClip();
    nonUnitQuat.Tracks[0].Values[3] = 5.0f;
    EXPECT_FALSE(ValidateAnimationClipData(nonUnitQuat, &error));

    AnimationClipData keyPastDuration = TwoKeyRotationClip();
    keyPastDuration.DurationSeconds = 0.5f; // last key at t=1.0
    EXPECT_FALSE(ValidateAnimationClipData(keyPastDuration, &error));
}

// -- Skinned .skmesh ----------------------------------------------------------

TEST(SkinnedMesh, RoundTripsThroughSkmesh)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    std::vector<std::byte> bytes;
    ASSERT_TRUE(serializer.WriteSkinnedToBytes(SkinnedTriangle(), bytes));

    MeshLoader loader(logging);
    SkinnedMeshData loaded;
    ASSERT_TRUE(loader.LoadSkinnedFromBytes(bytes, loaded));
    EXPECT_EQ(loaded.Skinning.SkeletonPath, "asset://chars/hero.glb#skel:rig");
    EXPECT_EQ(loaded.Skinning.JointCount, 2u);
    ASSERT_EQ(loaded.Skinning.Influences.size(), 3u);
    EXPECT_EQ(loaded.Skinning.Influences[0].Weights[0], 255);
    EXPECT_EQ(loaded.Geometry.Vertices.size(), 3u);
}

TEST(SkinnedMesh, ValidationRejectsBadInfluences)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    std::vector<std::byte> bytes;

    // Weights that do not sum to 255.
    SkinnedMeshData badWeights = SkinnedTriangle();
    badWeights.Skinning.Influences[0].Weights[0] = 100;
    EXPECT_FALSE(serializer.WriteSkinnedToBytes(badWeights, bytes));

    // A joint index past the joint count.
    SkinnedMeshData badJoint = SkinnedTriangle();
    badJoint.Skinning.Influences[0].Joints[0] = 7;
    EXPECT_FALSE(serializer.WriteSkinnedToBytes(badJoint, bytes));

    // A non-asset skeleton path.
    SkinnedMeshData badPath = SkinnedTriangle();
    badPath.Skinning.SkeletonPath = "not-a-path";
    EXPECT_FALSE(serializer.WriteSkinnedToBytes(badPath, bytes));
}

TEST(SkinnedMesh, StaticAndSkinnedPathsRejectEachOther)
{
    LoggingProvider logging;
    MeshSerializer serializer(logging);
    MeshLoader loader(logging);

    // A skinned container cannot be read through the static path...
    std::vector<std::byte> skinnedBytes;
    ASSERT_TRUE(serializer.WriteSkinnedToBytes(SkinnedTriangle(), skinnedBytes));
    MeshGeometry asStatic;
    EXPECT_FALSE(loader.LoadFromBytes(skinnedBytes, asStatic));

    // ...and a static container cannot be read through the skinned path.
    std::vector<std::byte> staticBytes;
    ASSERT_TRUE(serializer.WriteToBytes(SkinnedTriangle().Geometry, staticBytes));
    SkinnedMeshData asSkinned;
    EXPECT_FALSE(loader.LoadSkinnedFromBytes(staticBytes, asSkinned));

    // The static geometry still round-trips through its own path.
    MeshGeometry loaded;
    EXPECT_TRUE(loader.LoadFromBytes(staticBytes, loaded));
    EXPECT_EQ(loaded.Vertices.size(), 3u);
}

// -- Caches and the refcount chains ------------------------------------------

TEST(SkeletonCache, RegisterAcquireRelease)
{
    SkeletonCache cache;
    SkeletonHandle handle = cache.Register("asset://rig.sskel", TwoJointSkeleton());
    ASSERT_TRUE(handle.IsValid());

    const SkeletonData* data = cache.Get(handle);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->Joints.size(), 2u);

    // Re-acquire returns the same entry; one extra refcount to release.
    SkeletonHandle again = cache.Acquire("asset://rig.sskel");
    EXPECT_EQ(again, handle);

    cache.Release(again);
    EXPECT_NE(cache.Get(handle), nullptr); // still held by the first ref
    cache.Release(handle);
    EXPECT_EQ(cache.Get(handle), nullptr); // freed at refcount zero
}

TEST(AnimationClipCache, ClipReleaseFreesSkeletonChain)
{
    SkeletonCache skeletons;
    AnimationClipCache clips;

    SkeletonHandle skeleton = skeletons.Register("asset://rig.sskel", TwoJointSkeleton());
    ASSERT_TRUE(skeleton.IsValid());

    // The clip holds a skeleton reference (the clip→skeleton chain).
    SkeletonCacheHandle owned(&skeletons, skeleton, SkeletonCacheHandle::NoAttachTag{});
    AnimationClipHandle clip =
        clips.Register("asset://walk.sanim", TwoKeyRotationClip(), std::move(owned));
    ASSERT_TRUE(clip.IsValid());

    // The skeleton stays alive while the clip holds it...
    EXPECT_NE(skeletons.Get(skeleton), nullptr);

    // ...and releasing the last clip reference frees the whole chain.
    clips.Release(clip);
    EXPECT_EQ(clips.Get(clip), nullptr);
    EXPECT_EQ(skeletons.Get(skeleton), nullptr);
}

TEST(AnimationClipCache, SharedSkeletonSurvivesUntilLastHolderReleases)
{
    SkeletonCache skeletons;
    AnimationClipCache clips;

    SkeletonHandle skeleton = skeletons.Register("asset://rig.sskel", TwoJointSkeleton());

    SkeletonCacheHandle ownedA(&skeletons, skeletons.Acquire("asset://rig.sskel"),
                               SkeletonCacheHandle::NoAttachTag{});
    SkeletonCacheHandle ownedB(&skeletons, skeletons.Acquire("asset://rig.sskel"),
                               SkeletonCacheHandle::NoAttachTag{});
    AnimationClipHandle walk = clips.Register("asset://walk.sanim", TwoKeyRotationClip(), std::move(ownedA));
    AnimationClipHandle run = clips.Register("asset://run.sanim", TwoKeyRotationClip(), std::move(ownedB));

    // Release the original Register reference; two clips still hold it.
    skeletons.Release(skeleton);
    EXPECT_NE(skeletons.Get(skeleton), nullptr);

    clips.Release(walk);
    EXPECT_NE(skeletons.Get(skeleton), nullptr); // run still holds it
    clips.Release(run);
    EXPECT_EQ(skeletons.Get(skeleton), nullptr); // last holder gone
}
