#include <gtest/gtest.h>

#include "AllocationCounter.h"
#include "AttachmentFixture.h"

#include <anim/AnimPosePool.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimRig.h>
#include <anim/SkinningPalette.h>
#include <attach/Attachment.h>
#include <ecs/PartitionFollowers.h>
#include <math/MathSchemas.h>
#include <render/MeshOverrides.h>
#include <render/PresentedWith.h>
#include <render/StaticMeshComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    using attach_test::AttachFixture;
    using attach_test::kElsewhere;
    using attach_test::kRig;
    using attach_test::kZone;

    PresentationOwner Owner(std::uint32_t writer, std::uint64_t key)
    {
        return { .Writer = PresentationWriterId{ writer }, .Holder = LeaseOwner{ .Entity = {}, .Key = key } };
    }

    void ExpectNear(const Vec3d& actual, const Vec3d& expected)
    {
        EXPECT_NEAR(actual.X, expected.X, 1e-5f);
        EXPECT_NEAR(actual.Y, expected.Y, 1e-5f);
        EXPECT_NEAR(actual.Z, expected.Z, 1e-5f);
    }
}

TEST(Attachments, AnAttachmentFollowsTheJointAtThePoseItsBodyIsDrawnAt)
{
    AttachFixture fx;
    fx.Entities.AddComponent<WorldTransformHistory>(fx.Body);
    fx.Pose(fx.Body, Vec3d(1.0f, 0.0f, 0.0f), Vec3d(3.0f, 0.0f, 0.0f));
    const EntityId rifle = fx.Mesh("hand_r");

    PlaceAttachments(fx.Entities, 0.25);

    const World& reader = fx.Entities;
    const AnimPosePool::Slot& slot =
        *reader.TryGetResource<AnimPosePool>()->Find(reader.TryGet<AnimPoseState>(fx.Body)->Slot, fx.Body);
    const SkeletonData& skeleton = *fx.Skeletons.Get(fx.Skeleton);
    const Transform3f joint = Transform3f::FromMat4(slot.PresentedModel(skeleton, 0.25f)[2]);
    ExpectNear(fx.Local(rifle).Position, joint.Position + Vec3d(0.0f, 0.0f, 0.5f));
    // A quarter of the way from 1 to 3, one up the spine.
    ExpectNear(fx.Local(rifle).Position, Vec3d(1.5f, 1.0f, 0.5f));
    EXPECT_EQ(reader.TryGet<Parent>(rifle)->Entity, fx.Body);
    EXPECT_EQ(reader.TryGet<PresentedWith>(rifle)->Root, fx.Body);
}

TEST(Attachments, ABodyWithoutHistoryIsPlacedAtItsLastTickWhole)
{
    AttachFixture fx;
    fx.Pose(fx.Body, Vec3d(1.0f, 0.0f, 0.0f), Vec3d(3.0f, 0.0f, 0.0f));
    const EntityId rifle = fx.Mesh("hand_r");
    PlaceAttachments(fx.Entities, 0.25);
    ExpectNear(fx.Local(rifle).Position, Vec3d(3.0f, 1.0f, 0.5f));
}

TEST(Attachments, AnUnposedBodyCarriesItAtBind)
{
    AttachFixture fx;
    const EntityId rifle = fx.Mesh("hand_r");
    PlaceAttachments(fx.Entities, 1.0);
    ExpectNear(fx.Local(rifle).Position, Vec3d(0.0f, 2.0f, 0.5f));
}

TEST(Attachments, AnUnknownSocketIsRefusedAndNothingIsMade)
{
    AttachFixture fx;
    const std::size_t entities = fx.Entities.GetAliveEntities().size();
    AttachResult result = AttachResult::Attached;
    const EntityId none = CreateMeshAttachment(
        fx.Entities, { .Root = fx.Body, .Socket = "hand_l", .Mount = {}, .Placement = {}, .Mesh = {}, .Materials = {},
                       .Visible = true, .CastShadows = true },
        &result);
    EXPECT_EQ(result, AttachResult::SocketUnknown);
    EXPECT_FALSE(none.IsValid());
    EXPECT_EQ(fx.Entities.GetAliveEntities().size(), entities);
    EXPECT_TRUE(fx.Worn(fx.Body).empty());
}

TEST(Attachments, AnOriginAttachmentStaysWhereItWasPut)
{
    AttachFixture fx;
    fx.Pose(fx.Body, Vec3d::Zero(), Vec3d(3.0f, 0.0f, 0.0f));
    const EntityId badge = fx.Entities.CreateEntity(kZone);
    const Transform3f offset(Vec3d(0.0f, 1.5f, 0.2f), Quat<float>::Identity(), Vec3d::One());
    ASSERT_EQ(AttachToOrigin(fx.Entities, fx.Body, badge, { .Offset = offset, .Owner = {} }), AttachResult::Attached);
    PlaceAttachments(fx.Entities, 0.5);
    EXPECT_EQ(fx.Local(badge), offset);
}

TEST(Attachments, OneSocketHoldsManyAndEachEntityIsOneAttachment)
{
    AttachFixture fx;
    const std::size_t before = fx.Entities.GetAliveEntities().size();
    const EntityId first = fx.Mesh("hand_r", Owner(1, 1));
    const EntityId second = fx.Mesh("hand_r", Owner(1, 2));
    EXPECT_EQ(fx.Entities.GetAliveEntities().size(), before + 2) << "nothing for the joint or the socket";
    const std::vector<EntityId> worn = fx.Worn(fx.Body);
    EXPECT_EQ(worn.size(), 2u);
    EXPECT_EQ(FindAttachment(fx.Entities, fx.Body, Owner(1, 1)), first);
    EXPECT_EQ(FindAttachment(fx.Entities, fx.Body, Owner(1, 2)), second);
    EXPECT_FALSE(FindAttachment(fx.Entities, fx.Body, Owner(2, 1)).IsValid()) << "owners compare whole";
}

TEST(Attachments, DestroyingAMountEndsWhatIsMountedOnIt)
{
    AttachFixture fx;
    const EntityId rifle = fx.Mesh("hand_r");
    const EntityId scope = fx.Entities.CreateEntity(kZone);
    ASSERT_EQ(AttachToOrigin(fx.Entities, rifle, scope), AttachResult::Attached);
    const World& reader = fx.Entities;
    EXPECT_EQ(reader.TryGet<Attachment>(scope)->Root, fx.Body) << "listed on the body it is worn by";
    EXPECT_EQ(reader.TryGet<Parent>(scope)->Entity, rifle);

    DestroyAttachment(fx.Entities, rifle);
    EXPECT_FALSE(fx.Entities.IsAlive(scope));
    EXPECT_TRUE(fx.Worn(fx.Body).empty());
    EXPECT_EQ(reader.TryGet<AttachmentSet>(fx.Body)->Count, 0u);
}

TEST(Attachments, AnAttachmentDestroyedByAnyoneLeavesTheList)
{
    AttachFixture fx;
    const EntityId first = fx.Mesh("hand_r", Owner(1, 1));
    const EntityId second = fx.Mesh("hand_r", Owner(1, 2));
    fx.Entities.DestroyEntity(first);
    EXPECT_EQ(fx.Worn(fx.Body), std::vector<EntityId>{ second });
}

TEST(Attachments, WhatABodyWearsEndsWithItTheSameFrame)
{
    AttachFixture fx;
    const EntityId rifle = fx.Mesh("hand_r");
    const EntityId other = fx.MakeBody(kZone);
    fx.Entities.DestroyEntity(fx.Body);
    EXPECT_TRUE(fx.Entities.IsAlive(rifle)) << "a hook only queues it";
    PlaceAttachments(fx.Entities, 1.0);
    EXPECT_FALSE(fx.Entities.IsAlive(rifle));
    EXPECT_TRUE(fx.Entities.IsAlive(other));
}

TEST(Attachments, AZoneUnloadingTakesABodyAndWhatItWears)
{
    AttachFixture fx;
    const EntityId rifle = fx.Mesh("hand_r");
    EXPECT_EQ(fx.Entities.GetEntityPartition(rifle), kZone) << "made where its body lives";
    (void)fx.Entities.DestroyPartition(kZone);
    PlaceAttachments(fx.Entities, 1.0);
    EXPECT_FALSE(fx.Entities.IsAlive(rifle));
}

TEST(Attachments, WhatABodyWearsFollowsItToAnotherPartition)
{
    AttachFixture fx;
    const EntityId rifle = fx.Mesh("hand_r");
    const EntityId scope = fx.Entities.CreateEntity(kZone);
    ASSERT_EQ(AttachToOrigin(fx.Entities, rifle, scope), AttachResult::Attached);
    ASSERT_TRUE(fx.Entities.MoveEntityToPartition(fx.Body, kElsewhere));
    (void)DispatchPartitionMoves(fx.Entities);
    EXPECT_EQ(fx.Entities.GetEntityPartition(rifle), kElsewhere);
    EXPECT_EQ(fx.Entities.GetEntityPartition(scope), kElsewhere);
}

TEST(Attachments, ARigReloadRebindsAndALostSocketHoldsItsPlace)
{
    AttachFixture fx;
    fx.Pose(fx.Body, Vec3d(1.0f, 0.0f, 0.0f), Vec3d(1.0f, 0.0f, 0.0f));
    const EntityId rifle = fx.Mesh("hand_r");
    PlaceAttachments(fx.Entities, 1.0);
    const Transform3f atHand = fx.Local(rifle);

    fx.Reload(kRig, kAnimRigType, AttachFixture::RigJson(R"([ { "name": "hand_r", "joint": "spine" } ])"));
    fx.Pose(fx.Body, Vec3d(1.0f, 0.0f, 0.0f), Vec3d(1.0f, 0.0f, 0.0f));
    PlaceAttachments(fx.Entities, 1.0);
    PlaceAttachments(fx.Entities, 1.0);
    // Rebound to the spine.
    ExpectNear(fx.Local(rifle).Position, Vec3d(0.0f, 1.0f, 0.0f));

    fx.Reload(kRig, kAnimRigType, AttachFixture::RigJson("[]"));
    fx.Pose(fx.Body, Vec3d(5.0f, 0.0f, 0.0f), Vec3d(5.0f, 0.0f, 0.0f));
    const Transform3f atSpine = fx.Local(rifle);
    PlaceAttachments(fx.Entities, 1.0);
    PlaceAttachments(fx.Entities, 1.0);
    const World& reader = fx.Entities;
    EXPECT_EQ(reader.TryGet<Attachment>(rifle)->Binding, AttachmentBinding::Lost);
    EXPECT_EQ(fx.Local(rifle), atSpine) << "never moved to the origin";
    EXPECT_FALSE(atHand == atSpine);
}

TEST(Attachments, AWorldThatPresentsNothingHoldsNoAttachments)
{
    AttachFixture fx(false);
    AttachResult result = AttachResult::Attached;
    EXPECT_FALSE(CreateMeshAttachment(fx.Entities, { .Root = fx.Body, .Socket = "hand_r", .Mount = {}, .Placement = {},
                                                     .Mesh = {}, .Materials = {}, .Visible = true, .CastShadows = true },
                                      &result)
                     .IsValid());
    EXPECT_EQ(result, AttachResult::NotPresenting);
}

TEST(Attachments, AnEntityInterpolatedOnItsOwnIsNotWorn)
{
    AttachFixture fx;
    const EntityId other = fx.Entities.CreateEntity(kZone);
    fx.Entities.AddComponent<WorldTransformHistory>(other);
    EXPECT_EQ(AttachToSocket(fx.Entities, fx.Body, "hand_r", other), AttachResult::NotCosmetic);
}

TEST(Attachments, AnUpdateKeepsTheEntityAndMovesItInPlace)
{
    AttachFixture fx;
    const EntityId rifle = fx.Mesh("hand_r", Owner(1, 7));
    const AttachResult moved = UpdateMeshAttachment(
        fx.Entities, rifle,
        { .Root = fx.Body, .Socket = "", .Mount = {}, .Placement = { .Offset = {}, .Owner = Owner(1, 7) },
          .Mesh = {}, .Materials = {}, .Visible = false, .CastShadows = true });
    EXPECT_EQ(moved, AttachResult::Updated);
    const World& reader = fx.Entities;
    EXPECT_EQ(reader.TryGet<Attachment>(rifle)->Binding, AttachmentBinding::Origin);
    EXPECT_FALSE(reader.TryGet<StaticMeshComponent>(rifle)->Visible);
    EXPECT_EQ(fx.Worn(fx.Body), std::vector<EntityId>{ rifle });
}

TEST(Attachments, AWritersAttachmentsGoWithIt)
{
    AttachFixture fx;
    (void)fx.Mesh("hand_r", Owner(1, 1));
    const EntityId kept = fx.Mesh("hand_r", Owner(2, 1));
    DestroyAttachmentsOf(fx.Entities, PresentationWriterId{ 1 });
    EXPECT_EQ(fx.Worn(fx.Body), std::vector<EntityId>{ kept });
}

// Recorded on request (SENCHA_ATTACH_BENCH=1): the placement pass over 1,000
// joint attachments on 500 posed bodies, per frame.
TEST(AttachmentBench, PlacementPassPerFrame)
{
    if (std::getenv("SENCHA_ATTACH_BENCH") == nullptr)
        GTEST_SKIP() << "set SENCHA_ATTACH_BENCH=1 to time the placement pass";
    AttachFixture fx;
    std::vector<EntityId> bodies;
    for (int b = 0; b < 500; ++b)
    {
        const EntityId body = fx.MakeBody(kZone);
        fx.Entities.AddComponent<WorldTransformHistory>(body);
        fx.Pose(body, Vec3d(1.0f, 0.0f, 0.0f), Vec3d(2.0f, 0.0f, 0.0f));
        for (int a = 0; a < 2; ++a)
            ASSERT_TRUE(CreateMeshAttachment(fx.Entities, { .Root = body, .Socket = "hand_r", .Mount = {},
                                                            .Placement = {}, .Mesh = {}, .Materials = {},
                                                            .Visible = true, .CastShadows = true })
                            .IsValid());
    }
    PlaceAttachments(fx.Entities, 0.5);
    constexpr int kFrames = 200;
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < kFrames; ++frame)
        PlaceAttachments(fx.Entities, 0.25 + 0.5 * (frame % 2));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[ measured ] placement pass, 1000 attachments on 500 bodies: " << ms / kFrames << " ms/frame\n";
}

TEST(Attachments, ASteadyFrameAllocatesNothing)
{
    AttachFixture fx;
    fx.Entities.AddComponent<WorldTransformHistory>(fx.Body);
    fx.Pose(fx.Body, Vec3d(1.0f, 0.0f, 0.0f), Vec3d(2.0f, 0.0f, 0.0f));
    (void)fx.Mesh("hand_r", Owner(1, 1));
    (void)fx.Mesh("hand_r", Owner(1, 2));
    PlaceAttachments(fx.Entities, 0.5);
    const std::size_t before = AllocationCount();
    for (int frame = 0; frame < 10; ++frame)
        PlaceAttachments(fx.Entities, 0.1 * frame);
    EXPECT_EQ(AllocationCount() - before, 0u);
}
