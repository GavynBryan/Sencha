#include <ecs/Query.h>
#include <ecs/WorldComponentSchema.h>
#include <math/Angle.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformPropagation.h>
#include <zone/ResolvedLinks.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/ZonePackageImporter.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace
{
    constexpr ZoneParticipation kPlayed{ .Visible = true, .Physics = true, .Logic = true, .Audio = true };
    constexpr ZoneId kRoom{ 0x81 };
    constexpr ZoneId kHall{ 0x82 };
    constexpr LinkId kDoor{ 0xe1 };
    constexpr LinkId kWindow{ 0xe2 };
    constexpr LinkId kHatch{ 0xe0 };
    constexpr PersistentEntityId kRoomAperture{ 0x10 };
    constexpr PersistentEntityId kHallAperture{ 0x20 };
    constexpr PersistentEntityId kRoomSecond{ 0x11 };
    constexpr PersistentEntityId kHallSecond{ 0x21 };
    constexpr PersistentEntityId kRoomCrate{ 0x30 };

    WorldComponentSchema EngineSchema()
    {
        WorldComponentSchema schema;
        ComponentSerializerRegistry serializers;
        ComponentRegistrar registrar(&schema, &serializers, nullptr);
        RegisterEngineComponents(registrar);
        schema.Seal();
        return schema;
    }

    Transform3f At(Vec3d position, float yawDegrees = 0.0f)
    {
        return Transform3f{ position, Quatf::FromAxisAngle(Vec3d::Up(), ToRadians(yawDegrees)), Vec3d::One() };
    }

    struct AuthoredAperture
    {
        PersistentEntityId Id;
        Transform3f Pose;
        Vec2d Half{ 1.0f, 2.0f };
        bool IsAperture = true;
    };

    EntityBuildPackage Zone(const std::vector<AuthoredAperture>& apertures)
    {
        EntityBuildPackage package;
        for (const AuthoredAperture& aperture : apertures)
        {
            const PackageEntityId entity = package.CreateEntity();
            EXPECT_TRUE(package.AddComponent<PersistentIdComponent>(entity, { aperture.Id }));
            EXPECT_TRUE(package.SetPersistentId(entity, aperture.Id));
            EXPECT_TRUE(package.AddComponent<LocalTransform>(entity, LocalTransform{ aperture.Pose }));
            if (aperture.IsAperture)
            {
                EXPECT_TRUE(package.AddComponent<LinkAperture>(entity, LinkAperture{ aperture.Half }));
            }
        }
        return package;
    }

    WorldLink Link(LinkId id, ZoneId zoneA, PersistentEntityId a, ZoneId zoneB, PersistentEntityId b)
    {
        WorldLink link;
        link.Id = id;
        link.ZoneA = zoneA;
        link.ZoneB = zoneB;
        link.EndpointA.Target = a;
        link.EndpointB.Target = b;
        return link;
    }

    struct LinkWorld
    {
        LinkWorld() : Schema(EngineSchema()), Runtime(Schema) {}

        World& Entities() { return Runtime.Entities(); }
        ResolvedLinks& Links() { return Resolved; }

        void FinishResidency()
        {
            Runtime.FlushLifecycleRequests();
            (void)Runtime.BeginResidencyProcessing();
            Runtime.FinalizeResidencyProcessing();
        }

        void Import(ZoneId zone, const EntityBuildPackage& package)
        {
            ZoneImportError error;
            ASSERT_TRUE(ImportZonePackage(Runtime, Schema, zone, package, kPlayed, &error)) << error.Message;
            FinishResidency();
        }

        void Leave(ZoneId zone)
        {
            ASSERT_TRUE(Runtime.RequestDetach(zone));
            FinishResidency();
        }

        void Author(std::initializer_list<WorldLink> links)
        {
            EntityBuildPackage package;
            for (const WorldLink& link : links)
                EXPECT_TRUE(package.AddComponent<WorldLink>(package.CreateEntity(), link));
            ZoneImportError error;
            ASSERT_TRUE(ImportPackageIntoPartition(Entities(), Schema, package, PersistentStoragePartition,
                                                   ZoneStateScope{}, &error))
                << error.Message;
        }

        void RoomAndHall()
        {
            Import(kRoom, Zone({ { kRoomAperture, At(Vec3d{ 0, 1, 0 }) } }));
            Import(kHall, Zone({ { kHallAperture, At(Vec3d{ 40, 1, 0 }, 90.0f) } }));
        }

        // A frame of its own, so what this update wrote is behind the next.
        void Update()
        {
            Resolved.Update(Runtime);
            Entities().AdvanceFrame();
        }

        const LinkState& State(LinkId id) const
        {
            const auto states = Resolved.States();
            const auto found = std::ranges::find(states, id, &LinkState::Id);
            EXPECT_NE(found, states.end());
            return *found;
        }

        EntityId Resolve(PersistentEntityId id) { return Entities().GetResource<PersistentEntityIndex>().TryResolve(id); }

        WorldComponentSchema Schema;
        RuntimeWorld Runtime;
        ResolvedLinks Resolved;
    };

    std::vector<LinkTransition> Invalidations(const ResolvedLinks& links)
    {
        std::vector<LinkTransition> found;
        for (const LinkTransition& transition : links.Transitions())
            if (transition.To == LinkResolution::Invalid)
                found.push_back(transition);
        return found;
    }
}

TEST(ResolvedLinks, BothResidentResolvesActive)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    world.Update();

    ASSERT_EQ(world.Links().Active().size(), 1u);
    const ResolvedLink& link = world.Links().Active()[0];
    EXPECT_EQ(link.Id, kDoor);
    EXPECT_EQ(link.ApertureA, world.Resolve(kRoomAperture));
    EXPECT_EQ(link.ApertureB, world.Resolve(kHallAperture));
    EXPECT_EQ(link.ZoneA, kRoom);
    EXPECT_EQ(link.ZoneB, kHall);
    EXPECT_TRUE(link.Upright);
    const Vec3d mapped = link.AToB.TransformPoint(link.FrameA.Origin);
    EXPECT_NEAR(mapped.X, 40.0f, 1.0e-4f);
    EXPECT_NEAR(mapped.Y, 1.0f, 1.0e-4f);
    EXPECT_EQ(world.Links().Find(kDoor), &link);
    EXPECT_EQ(world.Links().Stats().Active, 1u);
    ASSERT_EQ(world.Links().Transitions().size(), 1u);
    EXPECT_EQ(world.Links().Transitions()[0].From, LinkResolution::Absent);
    EXPECT_EQ(world.Links().Transitions()[0].To, LinkResolution::Active);
}

TEST(ResolvedLinks, ASameZoneLinkResolves)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kRoom, kRoomSecond) });
    world.Import(kRoom, Zone({ { kRoomAperture, At(Vec3d{ 0, 1, 0 }) }, { kRoomSecond, At(Vec3d{ 9, 1, 0 }, 180.0f) } }));
    world.Update();
    ASSERT_EQ(world.Links().Active().size(), 1u);
    EXPECT_EQ(world.Links().Active()[0].ZoneA, world.Links().Active()[0].ZoneB);
}

TEST(ResolvedLinks, AnAbsentZoneAwaitsWithoutADiagnostic)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.Update();
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::AwaitingBoth);

    world.Import(kRoom, Zone({ { kRoomAperture, At(Vec3d{ 0, 1, 0 }) } }));
    world.Update();
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::AwaitingB);
    EXPECT_TRUE(Invalidations(world.Links()).empty());
    EXPECT_TRUE(world.Links().Active().empty());
    EXPECT_EQ(world.Links().Stats().Awaiting, 1u);

    world.Leave(kRoom);
    world.Import(kHall, Zone({ { kHallAperture, At(Vec3d{ 40, 1, 0 }) } }));
    world.Update();
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::AwaitingA);
    EXPECT_TRUE(Invalidations(world.Links()).empty());
}

TEST(ResolvedLinks, UnloadAndReloadResolvesAgainToNewLiveIds)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    world.Update();
    const EntityId before = world.Links().Active()[0].ApertureB;

    world.Leave(kHall);
    world.Update();
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::AwaitingB);
    EXPECT_TRUE(world.Links().Active().empty());

    world.Import(kHall, Zone({ { kHallAperture, At(Vec3d{ 40, 1, 0 }, 90.0f) } }));
    world.Update();
    ASSERT_EQ(world.Links().Active().size(), 1u);
    EXPECT_NE(world.Links().Active()[0].ApertureB, before) << "a reloaded aperture is a new entity";
    EXPECT_EQ(world.Links().Active()[0].ApertureB, world.Resolve(kHallAperture));
    EXPECT_TRUE(world.Entities().IsAlive(world.Links().Active()[0].ApertureB));
}

TEST(ResolvedLinks, WorldSceneBeforeOrAfterTheZonesGivesTheSameSpan)
{
    const auto links = { Link(kWindow, kRoom, kRoomSecond, kHall, kHallSecond),
                         Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture),
                         Link(kHatch, kRoom, PersistentEntityId{}, kHall, PersistentEntityId{}) };
    const auto zones = [](LinkWorld& world) {
        world.Import(kRoom, Zone({ { kRoomAperture, At(Vec3d{ 0, 1, 0 }) }, { kRoomSecond, At(Vec3d{ 5, 1, 0 }, 30.0f) } }));
        world.Import(kHall, Zone({ { kHallAperture, At(Vec3d{ 40, 1, 0 }, 90.0f) },
                                   { kHallSecond, At(Vec3d{ 45, 1, 3 }, -45.0f) } }));
    };
    LinkWorld first;
    first.Author(links);
    zones(first);
    first.Update();
    LinkWorld second;
    zones(second);
    second.Update();
    second.Author(links);
    second.Update();

    ASSERT_EQ(first.Links().Active().size(), 2u);
    ASSERT_EQ(second.Links().Active().size(), 2u);
    for (std::size_t index = 0; index < 2; ++index)
    {
        const ResolvedLink& a = first.Links().Active()[index];
        const ResolvedLink& b = second.Links().Active()[index];
        EXPECT_EQ(a.Id, b.Id);
        EXPECT_EQ(a.FrameA, b.FrameA);
        EXPECT_EQ(a.FrameB, b.FrameB);
        EXPECT_EQ(a.AToB, b.AToB);
        EXPECT_EQ(a.BToA, b.BToA);
    }
    EXPECT_EQ(first.Links().Active()[0].Id, kDoor) << "ordered by link id";
    EXPECT_EQ(first.Links().Active()[1].Id, kWindow);
    ASSERT_EQ(first.Links().States().size(), 3u);
    EXPECT_EQ(first.Links().States()[0].Id, kHatch);
    EXPECT_EQ(first.Links().States()[0].Resolution, LinkResolution::DemandOnly);
}

TEST(ResolvedLinks, AnApertureClaimedTwiceInvalidatesBothLinks)
{
    for (const bool doorFirst : { true, false })
    {
        LinkWorld world;
        const WorldLink door = Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture);
        const WorldLink window = Link(kWindow, kRoom, kRoomAperture, kHall, kHallSecond);
        if (doorFirst)
            world.Author({ door, window });
        else
            world.Author({ window, door });
        world.Import(kRoom, Zone({ { kRoomAperture, At(Vec3d{ 0, 1, 0 }) } }));
        world.Import(kHall, Zone({ { kHallAperture, At(Vec3d{ 40, 1, 0 }) }, { kHallSecond, At(Vec3d{ 45, 1, 0 }) } }));
        world.Update();
        EXPECT_TRUE(world.Links().Active().empty());
        for (const LinkId id : { kDoor, kWindow })
        {
            EXPECT_EQ(world.State(id).Resolution, LinkResolution::Invalid);
            EXPECT_EQ(world.State(id).Fault, LinkFault::ClaimedTwice);
        }
        ASSERT_EQ(Invalidations(world.Links()).size(), 2u);
        EXPECT_EQ(Invalidations(world.Links())[0].Id, kDoor);
        world.Update();
        world.Update();
        const std::uint64_t total = world.Links().Stats().TotalRebuilds;
        for (int frame = 0; frame < 5; ++frame)
            world.Update();
        EXPECT_EQ(world.Links().Stats().TotalRebuilds, total) << "a link faulted by authored data tracks no residency";
    }
}

TEST(ResolvedLinks, MalformedLinksAreInvalidAndDiagnosedOncePerTransition)
{
    struct Case
    {
        const char* Name;
        WorldLink Authored;
        std::vector<AuthoredAperture> Room;
        LinkFault Expected;
    };
    const Case cases[] = {
        { "one endpoint", Link(kDoor, kRoom, kRoomAperture, kHall, PersistentEntityId{}),
          { { kRoomAperture, At(Vec3d{}) } }, LinkFault::OneEndpoint },
        { "one aperture at both ends", Link(kDoor, kRoom, kRoomAperture, kRoom, kRoomAperture),
          { { kRoomAperture, At(Vec3d{}) } }, LinkFault::SameAperture },
        { "names something that is not an aperture", Link(kDoor, kRoom, kRoomCrate, kHall, kHallAperture),
          { { kRoomCrate, At(Vec3d{}), Vec2d{ 1, 2 }, false } }, LinkFault::NotAnAperture },
        { "names nothing in a resident zone", Link(kDoor, kRoom, kRoomSecond, kHall, kHallAperture),
          { { kRoomAperture, At(Vec3d{}) } }, LinkFault::NotAnAperture },
        { "names an aperture in another zone", Link(kDoor, kHall, kRoomAperture, kHall, kHallAperture),
          { { kRoomAperture, At(Vec3d{}) } }, LinkFault::ZoneMismatch },
        { "different sizes", Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture),
          { { kRoomAperture, At(Vec3d{}), Vec2d{ 1.5f, 2.0f } } }, LinkFault::ExtentsDiffer },
        { "zero extent", Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture),
          { { kRoomAperture, At(Vec3d{}), Vec2d{ 0.0f, 2.0f } } }, LinkFault::InvalidFrame },
    };
    for (const Case& c : cases)
    {
        LinkWorld world;
        world.Author({ c.Authored });
        world.Import(kRoom, Zone(c.Room));
        world.Import(kHall, Zone({ { kHallAperture, At(Vec3d{ 40, 1, 0 }) } }));
        world.Update();
        EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::Invalid) << c.Name;
        EXPECT_EQ(world.State(kDoor).Fault, c.Expected) << c.Name;
        EXPECT_EQ(Invalidations(world.Links()).size(), 1u) << c.Name;
        EXPECT_TRUE(world.Links().Active().empty()) << c.Name;
        world.Update();
        world.Update();
        EXPECT_TRUE(world.Links().Transitions().empty()) << c.Name << ": once per transition";
        EXPECT_EQ(world.Links().Stats().Rebuilds, 0u) << c.Name << ": a quiet frame does not rebuild";
    }
}

TEST(ResolvedLinks, AnApertureMovedAfterResolvingInvalidatesItsLink)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    world.Update();
    ASSERT_EQ(world.Links().Active().size(), 1u);

    world.Entities().TryGet<LocalTransform>(world.Resolve(kRoomAperture))->Value.Position.X += 0.5f;
    world.Update();
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::Invalid);
    EXPECT_EQ(world.State(kDoor).Fault, LinkFault::Moved);
    EXPECT_EQ(Invalidations(world.Links()).size(), 1u);
    world.Update();
    world.Update();
    EXPECT_EQ(world.State(kDoor).Fault, LinkFault::Moved) << "an endpoint stays where it resolved";
    EXPECT_TRUE(world.Links().Transitions().empty());

    world.Leave(kRoom);
    world.Import(kRoom, Zone({ { kRoomAperture, At(Vec3d{ 0.5f, 1, 0 }) } }));
    world.Update();
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::Active) << "a reloaded aperture resolves where it is";
}

TEST(ResolvedLinks, AQuietFrameDoesNotRebuild)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    world.Update();
    world.Update();
    world.Update();
    EXPECT_EQ(world.Links().Stats().Rebuilds, 0u);
    const std::uint64_t total = world.Links().Stats().TotalRebuilds;
    world.Update();
    EXPECT_EQ(world.Links().Stats().TotalRebuilds, total);
    ASSERT_EQ(world.Links().Active().size(), 1u);
}

TEST(ResolvedLinks, TheZeroLinkUpdateTouchesNoEntity)
{
    LinkWorld world;
    world.Import(kRoom, Zone({ { kRoomCrate, At(Vec3d{ 1, 0, 0 }), Vec2d{ 1, 2 }, false } }));
    world.Update();
    world.Update();
    const std::uint64_t structural = world.Entities().StructuralVersion();
    const std::uint32_t frame = world.Entities().CurrentFrame();
    world.Update();
    world.Update();
    EXPECT_EQ(world.Entities().StructuralVersion(), structural);
    EXPECT_EQ(world.Entities().CurrentFrame(), frame + 2);
    EXPECT_TRUE(world.Links().States().empty());
    EXPECT_TRUE(world.Links().Active().empty());
    EXPECT_EQ(world.Links().Stats().Rebuilds, 0u);

    Query<Changed<LocalTransform>> written(world.Entities());
    bool touched = false;
    written.ForEachChunk([&touched](auto&) { touched = true; }, frame - 1);
    EXPECT_FALSE(touched) << "no transform column written since before the quiet updates";
}

TEST(LinkContinuation, ASegmentThroughAnApertureContinuesFromTheOther)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    world.Update();
    const std::span<const ResolvedLink> links = world.Links().Active();
    const ResolvedLink& door = links[0];

    const Vec3d from = door.FrameA.Origin + door.FrameA.Normal * 1.0f;
    const Vec3d to = door.FrameA.Origin - door.FrameA.Normal * 3.0f;
    const std::optional<LinkContinuation> through = ContinueThroughLinks(links, from, to);
    ASSERT_TRUE(through.has_value());
    EXPECT_EQ(through->Link, &door);
    EXPECT_EQ(through->Entered, DockSide::A);
    EXPECT_NEAR(through->Crossing.Fraction, 0.25f, 1.0e-4f);
    EXPECT_NEAR((through->Exit - door.FrameB.Origin).Magnitude(), 0.0f, 1.0e-3f);
    const Vec3d expectedEnd = door.FrameB.Origin + door.FrameB.Normal * 3.0f;
    EXPECT_NEAR((through->End - expectedEnd).Magnitude(), 0.0f, 1.0e-3f);

    EXPECT_FALSE(ContinueThroughLinks(links, to, from).has_value()) << "an aperture is entered from its front only";
    const Vec3d beside = door.FrameA.Origin + door.FrameA.Right * 3.0f;
    EXPECT_FALSE(ContinueThroughLinks(links, beside + door.FrameA.Normal, beside - door.FrameA.Normal).has_value());

    const std::optional<LinkContinuation> back = ContinueThroughLinks(
        links, door.FrameB.Origin + door.FrameB.Normal, door.FrameB.Origin - door.FrameB.Normal);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->Entered, DockSide::B);
    EXPECT_NEAR((back->Exit - door.FrameA.Origin).Magnitude(), 0.0f, 1.0e-3f);
}

TEST(LinkContinuation, OneWayLinksAndTheEarliestCrossingDecide)
{
    LinkWorld world;
    WorldLink door = Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture);
    door.Directions = DockDirectionAToB;
    WorldLink window = Link(kWindow, kRoom, kRoomSecond, kHall, kHallSecond);
    world.Author({ door, window });
    world.Import(kRoom, Zone({ { kRoomAperture, At(Vec3d{ 0, 1, 0 }) }, { kRoomSecond, At(Vec3d{ 0, 1, 4 }) } }));
    world.Import(kHall, Zone({ { kHallAperture, At(Vec3d{ 40, 1, 0 }) }, { kHallSecond, At(Vec3d{ 60, 1, 0 }) } }));
    world.Update();
    const std::span<const ResolvedLink> links = world.Links().Active();
    ASSERT_EQ(links.size(), 2u);

    // Along +Z from in front of both, whose fronts face -Z: the door's aperture comes first.
    const std::optional<LinkContinuation> first =
        ContinueThroughLinks(links, Vec3d{ 0, 1, -2 }, Vec3d{ 0, 1, 8 });
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->Link->Id, kDoor);

    const Vec3d hallFront = links[0].FrameB.Origin + links[0].FrameB.Normal;
    EXPECT_FALSE(ContinueThroughLinks(links, hallFront, links[0].FrameB.Origin - links[0].FrameB.Normal).has_value())
        << "the door goes from the room to the hall only";
}

TEST(ResolvedLinks, AnApertureMovedByItsParentInvalidatesItsLink)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    const EntityId parent = world.Entities().CreateEntity();
    world.Entities().AddComponent<LocalTransform>(parent, LocalTransform{ Transform3f::Identity() });
    world.Entities().AddComponent<WorldTransform>(parent, WorldTransform{ Transform3f::Identity() });
    world.Entities().AddComponent<Parent>(world.Resolve(kRoomAperture), Parent{ parent });
    PropagateTransforms(world.Entities());
    world.Update();
    world.Update();
    world.Update();
    ASSERT_EQ(world.State(kDoor).Resolution, LinkResolution::Active);

    world.Entities().TryGet<LocalTransform>(parent)->Value.Position.X += 3.0f;
    PropagateTransforms(world.Entities());
    world.Update();
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::Invalid) << "the aperture's world frame moved";
    EXPECT_EQ(world.State(kDoor).Fault, LinkFault::Moved);
}

TEST(ResolvedLinks, AWriteThatChangesNothingKeepsTheLinkActive)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    world.Update();
    world.Update();
    Query<Write<LocalTransform>, With<LinkAperture>> written(world.Entities());
    written.ForEachChunk([](auto& view) { (void)view.template Write<LocalTransform>(); });
    world.Update();
    EXPECT_EQ(world.Links().Stats().Rebuilds, 1u) << "the write is seen";
    EXPECT_EQ(world.State(kDoor).Resolution, LinkResolution::Active) << "and the frame it compares is the same";
    EXPECT_TRUE(world.Links().Transitions().empty());
}

TEST(ResolvedLinks, AMoveLaterInTheFrameOfAnUpdateIsSeen)
{
    LinkWorld world;
    world.Author({ Link(kDoor, kRoom, kRoomAperture, kHall, kHallAperture) });
    world.RoomAndHall();
    world.Update();
    world.Update();
    world.Links().Update(world.Runtime);
    world.Entities().TryGet<LocalTransform>(world.Resolve(kRoomAperture))->Value.Position.X += 0.5f;
    world.Entities().AdvanceFrame();
    world.Update();
    EXPECT_EQ(world.State(kDoor).Fault, LinkFault::Moved);
}

namespace
{
    ResolvedLink HandBuilt(LinkId id, const ApertureFrame& a, const ApertureFrame& b)
    {
        ResolvedLink link;
        link.Id = id;
        link.FrameA = a;
        link.FrameB = b;
        link.AToB = ApertureMapping(a, b);
        link.BToA = ApertureMapping(b, a);
        return link;
    }
}

TEST(LinkContinuation, AnEqualCrossingGoesToTheLowerIdThenToSideA)
{
    const ApertureFrame wall = MakeApertureFrame(At(Vec3d{ 0, 1, 0 }), Vec2d{ 1, 2 });
    const ApertureFrame far = MakeApertureFrame(At(Vec3d{ 40, 1, 0 }), Vec2d{ 1, 2 });
    const ApertureFrame farther = MakeApertureFrame(At(Vec3d{ 80, 1, 0 }), Vec2d{ 1, 2 });
    const Vec3d from{ 0, 1, -1 };
    const Vec3d to{ 0, 1, 1 };

    const ResolvedLink twoLinks[] = { HandBuilt(kDoor, wall, far), HandBuilt(kWindow, wall, farther) };
    const std::optional<LinkContinuation> lower = ContinueThroughLinks(twoLinks, from, to);
    ASSERT_TRUE(lower.has_value());
    EXPECT_EQ(lower->Link->Id, kDoor);

    const ResolvedLink oneLink[] = { HandBuilt(kDoor, wall, wall) };
    const std::optional<LinkContinuation> sideA = ContinueThroughLinks(oneLink, from, to);
    ASSERT_TRUE(sideA.has_value());
    EXPECT_EQ(sideA->Entered, DockSide::A);
}
