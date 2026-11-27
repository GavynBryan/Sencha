// A spatial link's cook: each endpoint carries its own aperture's frame, a
// link may join one zone to itself, the output does not depend on input
// order, and every authored-link validation rule fires on its case alone.

#include <core/json/JsonStringify.h>
#include <math/Angle.h>
#include <zone/WorldLinkValidation.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionValidation.h>
#include <zone/WorldTopologyCook.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr ZoneId kRoom{ 0xa1 };
    constexpr ZoneId kHall{ 0xa2 };
    constexpr ZoneId kCellar{ 0xa3 };
    constexpr PersistentEntityId kRoomAperture{ 0x10 };
    constexpr PersistentEntityId kRoomSecond{ 0x11 };
    constexpr PersistentEntityId kHallAperture{ 0x20 };
    constexpr PersistentEntityId kHallSecond{ 0x21 };

    WorldPartitionManifest Manifest()
    {
        WorldPartitionManifest manifest;
        manifest.Graphs = { GraphRecord{ GraphId{ 0xb1 }, "Interior" } };
        const auto zone = [](ZoneId id, Vec3d min, Vec3d max) {
            ZoneHeader header;
            header.Id = id;
            header.Graph = GraphId{ 0xb1 };
            header.Bounds = Aabb3d::FromMinMax(min, max);
            return header;
        };
        manifest.Zones = { zone(kRoom, Vec3d{ -10, -1, -10 }, Vec3d{ 10, 5, 10 }),
                           zone(kHall, Vec3d{ 30, -1, -10 }, Vec3d{ 50, 5, 10 }),
                           zone(kCellar, Vec3d{ 5, -1, -10 }, Vec3d{ 25, 5, 10 }) };
        manifest.StartZone = kRoom;
        return manifest;
    }

    Transform3f At(Vec3d position, Quatf rotation = Quatf::Identity())
    {
        return Transform3f{ position, rotation, Vec3d::One() };
    }

    AuthoredApertureCookInput Aperture(PersistentEntityId id, ZoneId zone, Transform3f pose,
                                       Vec2d half = Vec2d{ 1.0f, 2.0f })
    {
        return AuthoredApertureCookInput{ .Id = id, .Zone = zone, .Aperture = LinkAperture{ half }, .Transform = pose };
    }

    AuthoredLinkCookInput Link(std::uint64_t id, ZoneId zoneA, PersistentEntityId a, ZoneId zoneB, PersistentEntityId b)
    {
        WorldLink link;
        link.Id = LinkId{ id };
        link.ZoneA = zoneA;
        link.ZoneB = zoneB;
        link.EndpointA.Target = a;
        link.EndpointB.Target = b;
        return AuthoredLinkCookInput{ .Link = link, .AuthoredEntity = id };
    }

    std::vector<AuthoredApertureCookInput> Apertures()
    {
        return { Aperture(kRoomAperture, kRoom, At(Vec3d{ 0, 1, 0 })),
                 Aperture(kRoomSecond, kRoom, At(Vec3d{ 6, 1, 0 }, Quatf::FromAxisAngle(Vec3d::Up(), ToRadians(180.0f)))),
                 Aperture(kHallAperture, kHall, At(Vec3d{ 40, 1, 0 }, Quatf::FromAxisAngle(Vec3d::Up(), ToRadians(90.0f)))),
                 Aperture(kHallSecond, kHall, At(Vec3d{ 44, 1, 0 })) };
    }

    const std::vector<ZoneId> kAllZones{ kRoom, kHall, kCellar };

    std::vector<std::string> Rules(const std::vector<ContentRiskRecord>& records)
    {
        std::vector<std::string> rules;
        for (const ContentRiskRecord& record : records)
            rules.push_back(record.RuleId);
        return rules;
    }
}

TEST(WorldLinkCook, EachEndpointCarriesItsOwnAperturesFrame)
{
    WorldPartitionManifest manifest = Manifest();
    const AuthoredLinkCookInput link = Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture);
    const std::vector<AuthoredApertureCookInput> apertures = Apertures();
    std::string error;
    ASSERT_TRUE(CookWorldTopology(manifest, {}, std::span{ &link, 1 }, apertures, &error)) << error;

    ASSERT_EQ(manifest.Zones[0].Links.size(), 1u);
    ASSERT_EQ(manifest.Zones[1].Links.size(), 1u);
    const LinkEndpoint& a = manifest.Zones[0].Links[0];
    const LinkEndpoint& b = manifest.Zones[1].Links[0];
    ASSERT_TRUE(a.Aperture.has_value());
    ASSERT_TRUE(b.Aperture.has_value());
    EXPECT_EQ(*a.Aperture, MakeApertureFrame(apertures[0].Transform, apertures[0].Aperture.HalfExtents));
    EXPECT_EQ(*b.Aperture, MakeApertureFrame(apertures[2].Transform, apertures[2].Aperture.HalfExtents));
    EXPECT_TRUE(std::ranges::none_of(ValidateWorldPartitionManifest(manifest, WorldPartitionIndex::Build(manifest)),
                                     [](const ContentRiskRecord& record) {
                                         return record.RuleId == "partition.link.endpoint_invalid";
                                     }));
}

TEST(WorldLinkCook, ADemandOnlyLinkCooksNoFrames)
{
    WorldPartitionManifest manifest = Manifest();
    const AuthoredLinkCookInput link = Link(0xe1, kRoom, PersistentEntityId{}, kHall, PersistentEntityId{});
    ASSERT_TRUE(CookWorldTopology(manifest, {}, std::span{ &link, 1 }, Apertures()));
    EXPECT_FALSE(manifest.Zones[0].Links[0].Aperture.has_value());
    EXPECT_FALSE(manifest.Zones[1].Links[0].Aperture.has_value());
}

TEST(WorldLinkCook, ASpatialLinkMayJoinAZoneToItself)
{
    WorldPartitionManifest manifest = Manifest();
    const AuthoredLinkCookInput link = Link(0xe1, kRoom, kRoomAperture, kRoom, kRoomSecond);
    std::string error;
    ASSERT_TRUE(CookWorldTopology(manifest, {}, std::span{ &link, 1 }, Apertures(), &error)) << error;
    ASSERT_EQ(manifest.Zones[0].Links.size(), 2u);
    EXPECT_EQ(manifest.Zones[0].Links[0].Side, DockSide::A);
    EXPECT_EQ(manifest.Zones[0].Links[1].Side, DockSide::B);
    EXPECT_EQ(manifest.Zones[0].Links[0].OtherZone, kRoom);
    const std::vector<ContentRiskRecord> records =
        ValidateWorldPartitionManifest(manifest, WorldPartitionIndex::Build(manifest));
    EXPECT_TRUE(std::ranges::none_of(records, [](const ContentRiskRecord& record) {
        return record.RuleId == "partition.link.endpoint_invalid";
    }));

    WorldPartitionManifest demandOnly = Manifest();
    const AuthoredLinkCookInput loop = Link(0xe2, kRoom, PersistentEntityId{}, kRoom, PersistentEntityId{});
    EXPECT_FALSE(CookWorldTopology(demandOnly, {}, std::span{ &loop, 1 }, {}, &error))
        << "a demand edge from a zone to itself means nothing";
}

TEST(WorldLinkCook, OutputIsByteIdenticalAcrossInputOrder)
{
    const std::vector<AuthoredLinkCookInput> links = { Link(0xe2, kRoom, kRoomSecond, kHall, kHallSecond),
                                                       Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture),
                                                       Link(0xe3, kRoom, PersistentEntityId{}, kHall, PersistentEntityId{}) };
    std::vector<AuthoredLinkCookInput> reversedLinks(links.rbegin(), links.rend());
    const std::vector<AuthoredApertureCookInput> apertures = Apertures();
    std::vector<AuthoredApertureCookInput> reversedApertures(apertures.rbegin(), apertures.rend());

    WorldPartitionManifest first = Manifest();
    WorldPartitionManifest second = Manifest();
    ASSERT_TRUE(CookWorldTopology(first, {}, links, apertures));
    ASSERT_TRUE(CookWorldTopology(second, {}, reversedLinks, reversedApertures));
    EXPECT_EQ(JsonStringify(WriteWorldPartitionManifest(first), true),
              JsonStringify(WriteWorldPartitionManifest(second), true));
    EXPECT_EQ(Rules(ValidateAuthoredLinks(first, {}, links, apertures, kAllZones)),
              Rules(ValidateAuthoredLinks(second, {}, reversedLinks, reversedApertures, kAllZones)));
}

TEST(WorldLinkCook, AWellFormedLinkPassesValidation)
{
    const std::vector<AuthoredLinkCookInput> links = { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture),
                                                       Link(0xe2, kRoom, kRoomSecond, kHall, kHallSecond) };
    EXPECT_TRUE(ValidateAuthoredLinks(Manifest(), {}, links, Apertures(), kAllZones).empty());
}

TEST(WorldLinkCook, EveryValidationRuleFiresOnItsCase)
{
    const Quatf onItsBack = Quatf::FromAxisAngle(Vec3d::Right(), ToRadians(90.0f));
    struct Case
    {
        const char* Name;
        std::vector<AuthoredLinkCookInput> Links;
        std::vector<AuthoredApertureCookInput> Apertures;
        std::vector<std::string> Expected;
    };
    std::vector<AuthoredDockCookInput> noDocks;
    const std::vector<Case> cases = {
        { "one endpoint",
          { Link(0xe1, kRoom, kRoomAperture, kHall, PersistentEntityId{}) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{})) },
          { "partition.link.endpoint_one" } },
        { "names no aperture",
          { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{})) },
          { "partition.link.endpoint_not_aperture" } },
        { "one aperture at both ends",
          { Link(0xe1, kRoom, kRoomAperture, kRoom, kRoomAperture) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{})) },
          { "partition.link.endpoint_same" } },
        { "an aperture two links name",
          { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture), Link(0xe2, kRoom, kRoomAperture, kHall, kHallSecond) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{})), Aperture(kHallAperture, kHall, At(Vec3d{ 40, 0, 0 })),
            Aperture(kHallSecond, kHall, At(Vec3d{ 44, 0, 0 })) },
          { "partition.link.aperture_claimed_twice", "partition.link.aperture_claimed_twice" } },
        { "an invalid frame",
          { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture) },
          { AuthoredApertureCookInput{ .Id = kRoomAperture, .Zone = kRoom, .Aperture = LinkAperture{},
                                       .Transform = Transform3f{ Vec3d{}, Quatf::Identity(), Vec3d{ 2, 2, 2 } } },
            Aperture(kHallAperture, kHall, At(Vec3d{ 40, 0, 0 })) },
          { "partition.link.aperture_invalid" } },
        { "different sizes",
          { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{}), Vec2d{ 1.0f, 2.0f }),
            Aperture(kHallAperture, kHall, At(Vec3d{ 40, 0, 0 }), Vec2d{ 1.0f, 2.5f }) },
          { "partition.link.extents_differ" } },
        { "zones that disagree with the apertures",
          { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture) },
          { Aperture(kRoomAperture, kCellar, At(Vec3d{ 20, 0, 0 })), Aperture(kHallAperture, kHall, At(Vec3d{ 40, 0, 0 })) },
          { "partition.link.zone_mismatch" } },
        { "overlapping zones with no dock between",
          { Link(0xe1, kRoom, kRoomAperture, kCellar, kHallAperture) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{})), Aperture(kHallAperture, kCellar, At(Vec3d{ 20, 0, 0 })) },
          { "partition.link.zones_overlap" } },
        { "a link that tips a walker over",
          { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{})), Aperture(kHallAperture, kHall, At(Vec3d{ 40, 0, 0 }, onItsBack)) },
          { "partition.link.not_upright" } },
        { "an aperture no link names",
          { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture) },
          { Aperture(kRoomAperture, kRoom, At(Vec3d{})), Aperture(kHallAperture, kHall, At(Vec3d{ 40, 0, 0 })),
            Aperture(kRoomSecond, kRoom, At(Vec3d{ 3, 0, 0 })) },
          { "partition.link.aperture_unlinked" } },
    };
    for (const Case& c : cases)
        EXPECT_EQ(Rules(ValidateAuthoredLinks(Manifest(), noDocks, c.Links, c.Apertures, kAllZones)), c.Expected)
            << c.Name;
}

TEST(WorldLinkCook, ADockBetweenOverlappingZonesExcusesTheirLink)
{
    const std::vector<AuthoredLinkCookInput> links = { Link(0xe1, kRoom, kRoomAperture, kCellar, kHallAperture) };
    const std::vector<AuthoredApertureCookInput> apertures = { Aperture(kRoomAperture, kRoom, At(Vec3d{})),
                                                               Aperture(kHallAperture, kCellar, At(Vec3d{ 20, 0, 0 })) };
    AuthoredDockCookInput dock;
    dock.Dock.Id = DockId{ 0xd1 };
    dock.Dock.ZoneA = kCellar;
    dock.Dock.ZoneB = kRoom;
    EXPECT_TRUE(ValidateAuthoredLinks(Manifest(), std::span{ &dock, 1 }, links, apertures, kAllZones).empty());
}

TEST(WorldLinkCook, AReferenceIntoAnUncollectedZoneIsNotJudged)
{
    const std::vector<AuthoredLinkCookInput> links = { Link(0xe1, kRoom, kRoomAperture, kHall, kHallAperture) };
    const std::vector<AuthoredApertureCookInput> roomOnly = { Aperture(kRoomAperture, kRoom, At(Vec3d{})) };
    const std::vector<ZoneId> collected{ kRoom };
    EXPECT_TRUE(ValidateAuthoredLinks(Manifest(), {}, links, roomOnly, collected).empty());
}

TEST(WorldLinkCook, RecordsAreSortedByLinkThenRule)
{
    const std::vector<AuthoredLinkCookInput> links = {
        Link(0xe2, kRoom, kRoomAperture, kHall, PersistentEntityId{}),
        Link(0xe1, kRoom, kRoomSecond, kHall, kHallAperture),
    };
    const std::vector<AuthoredApertureCookInput> apertures = {
        Aperture(kRoomSecond, kRoom, At(Vec3d{}), Vec2d{ 1.0f, 1.0f }),
        Aperture(kHallAperture, kHall, At(Vec3d{ 40, 0, 0 }, Quatf::FromAxisAngle(Vec3d::Right(), ToRadians(90.0f)))),
    };
    const std::vector<ContentRiskRecord> records = ValidateAuthoredLinks(Manifest(), {}, links, apertures, kAllZones);
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].SourceId, 0xe1u);
    EXPECT_EQ(records[0].RuleId, "partition.link.extents_differ");
    EXPECT_EQ(records[1].SourceId, 0xe1u);
    EXPECT_EQ(records[1].RuleId, "partition.link.not_upright");
    EXPECT_EQ(records[2].SourceId, 0xe2u);
    EXPECT_EQ(records[2].RuleId, "partition.link.endpoint_one");
    EXPECT_EQ(records[0].Severity, ContentRiskSeverity::Error);
    EXPECT_EQ(records[1].Severity, ContentRiskSeverity::Warning);
}
