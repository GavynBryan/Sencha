// The handoff from the world cook to the runtime, end to end.
//
// The cook compiles authored dock and link components into reciprocal endpoint
// pairs; validation independently re-derives what a well-formed pair looks like;
// the runtime builds adjacency from whatever survived and sweeps a focus through
// it. Each of those is covered on its own. Nothing covered the seam, so the
// mirror convention was written down twice — once where endpoints are produced
// and once where they are checked — with no test binding the two together. A
// change to one that missed the other would pass every existing suite.
//
// These drive authored components all the way to a crossing, through real
// serialization in between, so the convention is asserted by consequence rather
// than by repeating the literal in a third place.

#include <gtest/gtest.h>

#include <core/json/JsonValue.h>
#include <zone/DockCrossing.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>
#include <zone/WorldPartitionRuntime.h>
#include <zone/WorldPartitionValidation.h>
#include <zone/WorldTopologyCook.h>

#include "ZoneTopologyStressFixture.h"

#include <numbers>
#include <string>
#include <vector>

using namespace ZoneTopologyStress;

namespace
{
// Zones only: the topology the cook is about to add is deliberately absent.
TopologySpec BareChain(int zoneCount)
{
    return TopologySpec{ .Shape = TopologyShape::Chain,
                         .ZoneCount = zoneCount,
                         .Docks = false,
                         .Links = false };
}

// A dock authored on the boundary zone `lower` shares with `lower + 1`, facing
// +X, which is how the generator lays a chain out.
AuthoredDockCookInput AuthoredDock(const TopologySpec& spec, int lower,
                                   std::uint64_t id)
{
    const Aabb3d bounds = StressZoneBounds(spec, lower);

    AuthoredDockCookInput input;
    input.Dock.Id = DockId{ id };
    input.Dock.ZoneA = StressZoneAt(lower);
    input.Dock.ZoneB = StressZoneAt(lower + 1);
    input.Dock.HalfExtents = Vec2d{ static_cast<float>(spec.ZoneSpan * 0.5),
                                    static_cast<float>(spec.ZoneHeight * 0.5) };
    input.Dock.Directions = DockDirectionBoth;
    input.AuthoredEntity = 0x1000 + id;

    // The cook takes the plane's normal from the transform's backward axis, so
    // a chain boundary that separates zones along X needs a quarter turn about
    // up: forward (0,0,-1) becomes (-1,0,0) and the normal it yields is +X.
    input.Transform = Transform3f{};
    input.Transform.Rotation =
        Quatf::FromAxisAngle(Vec3d::Up(), std::numbers::pi_v<float> * 0.5f);
    input.Transform.Position = Vec3d{ static_cast<float>(bounds.Max.X),
                                     (bounds.Min.Y + bounds.Max.Y) * 0.5f,
                                     (bounds.Min.Z + bounds.Max.Z) * 0.5f };
    return input;
}

bool HasRule(const std::vector<ContentRiskRecord>& records, const char* rule)
{
    for (const ContentRiskRecord& record : records)
        if (record.RuleId == rule)
            return true;
    return false;
}

std::vector<ContentRiskRecord> Errors(const std::vector<ContentRiskRecord>& records)
{
    std::vector<ContentRiskRecord> errors;
    for (const ContentRiskRecord& record : records)
        if (record.Severity == ContentRiskSeverity::Error)
            errors.push_back(record);
    return errors;
}
}  // namespace

// The whole path: author, cook, serialize, parse, validate, load, cross. If the
// cook's mirror convention and the runtime's expectations ever disagree, the
// crossing at the end is what stops working.
TEST(WorldTopologyCookSeam, AuthoredDocksSurviveCookSerializationAndDriveACrossing)
{
    const TopologySpec spec = BareChain(3);
    WorldPartitionManifest manifest = BuildTopology(spec);

    const AuthoredDockCookInput docks[] = { AuthoredDock(spec, 0, 0x11),
                                            AuthoredDock(spec, 1, 0x12) };
    std::string cookError;
    ASSERT_TRUE(CookWorldTopology(manifest, docks, {}, &cookError)) << cookError;

    // Through the file format the cook actually writes.
    const JsonValue written = WriteWorldPartitionManifest(manifest);
    std::string readError;
    const std::optional<WorldPartitionManifest> reloaded =
        ReadWorldPartitionManifest(written, &readError);
    ASSERT_TRUE(reloaded.has_value()) << readError;

    const WorldPartitionIndex index = WorldPartitionIndex::Build(*reloaded);
    const std::vector<ContentRiskRecord> records =
        ValidateWorldPartitionManifest(*reloaded, index);
    EXPECT_TRUE(Errors(records).empty())
        << "cooked topology failed the validation the cook is written against";

    // The runtime accepts it.
    WorldPartitionRuntime partition(
        [](const ZoneHeader&) { return ZoneLoadRecipe{}; },
        WorldPartitionStreamingConfig{});
    std::string loadError;
    ASSERT_TRUE(partition.LoadManifest(*reloaded, &loadError)) << loadError;

    // And a sweep across the authored plane moves the focus, which only happens
    // if the endpoint the runtime reads faces the way the cook meant.
    ZoneFocusState state;
    state.Current = StressZoneAt(0);
    state.PreviousPosition = StressZoneCenter(spec, 0);

    const DockTraversalResult result =
        AdvanceZoneFocus(state, index, StressZoneCenter(spec, 1));

    EXPECT_EQ(result.Status, DockTraversalStatus::Crossed);
    EXPECT_EQ(result.From, StressZoneAt(0));
    EXPECT_EQ(result.To, StressZoneAt(1));
    EXPECT_EQ(state.Current, StressZoneAt(1));
}

// The cook emits side B by negating the normal and the right vector and keeping
// up. Validation requires exactly that. This is the test that fails if either
// side of that agreement is edited alone.
TEST(WorldTopologyCookSeam, AnUnmirroredEndpointIsRejectedByValidation)
{
    const TopologySpec spec = BareChain(2);
    WorldPartitionManifest manifest = BuildTopology(spec);

    const AuthoredDockCookInput docks[] = { AuthoredDock(spec, 0, 0x21) };
    std::string cookError;
    ASSERT_TRUE(CookWorldTopology(manifest, docks, {}, &cookError)) << cookError;

    {
        const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
        ASSERT_TRUE(Errors(ValidateWorldPartitionManifest(manifest, index)).empty())
            << "the cooked pair was rejected before it was damaged";
    }

    // Break only the mirror: keep both endpoints, both zones, both ids.
    ZoneHeader& zoneB = manifest.Zones[1];
    ASSERT_EQ(zoneB.Docks.size(), 1u);
    zoneB.Docks[0].Right = -zoneB.Docks[0].Right;

    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    const std::vector<ContentRiskRecord> records =
        ValidateWorldPartitionManifest(manifest, index);

    EXPECT_TRUE(HasRule(records, "partition.dock.endpoint_invalid"))
        << "an endpoint pair that no longer mirrors was accepted";
}

// An endpoint naming a zone that is not in the manifest. Adjacency copies
// endpoints without rechecking the far zone, and a focus sweep assigns the far
// zone unconditionally, so validation is the only thing standing between a
// dangling reference and a focus pointing at nothing.
TEST(WorldTopologyCookSeam, AnEndpointNamingAMissingZoneIsRejectedByValidation)
{
    const TopologySpec spec = BareChain(2);
    WorldPartitionManifest manifest = BuildTopology(spec);

    const AuthoredDockCookInput docks[] = { AuthoredDock(spec, 0, 0x31) };
    std::string cookError;
    ASSERT_TRUE(CookWorldTopology(manifest, docks, {}, &cookError)) << cookError;

    const ZoneId missing{ kZoneIdBase + 999 };
    for (DockEndpoint& endpoint : manifest.Zones[0].Docks)
        endpoint.OtherZone = missing;

    const WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    const std::vector<ContentRiskRecord> records =
        ValidateWorldPartitionManifest(manifest, index);

    EXPECT_TRUE(HasRule(records, "partition.dock.endpoint_invalid"))
        << "an endpoint pointing at a zone that does not exist was accepted";
}

// The cook refuses a dock whose zones are not both real, rather than emitting a
// pair for validation to catch later.
TEST(WorldTopologyCookSeam, TheCookRefusesADockNamingAMissingZone)
{
    const TopologySpec spec = BareChain(2);
    WorldPartitionManifest manifest = BuildTopology(spec);

    AuthoredDockCookInput dock = AuthoredDock(spec, 0, 0x41);
    dock.Dock.ZoneB = ZoneId{ kZoneIdBase + 999 };

    const AuthoredDockCookInput docks[] = { dock };
    std::string cookError;
    EXPECT_FALSE(CookWorldTopology(manifest, docks, {}, &cookError));
    EXPECT_FALSE(cookError.empty()) << "the cook refused without saying why";
}

// Authored links carry no geometry, so their seam is the id and zone pairing
// surviving the round trip and showing up as adjacency the demand BFS can walk.
TEST(WorldTopologyCookSeam, AuthoredLinksSurviveCookAndAppearAsAdjacency)
{
    const TopologySpec spec = BareChain(3);
    WorldPartitionManifest manifest = BuildTopology(spec);

    AuthoredLinkCookInput link;
    link.Link.Id = LinkId{ 0x51 };
    link.Link.ZoneA = StressZoneAt(0);
    link.Link.ZoneB = StressZoneAt(2);
    link.Link.Kind = LinkKind::Teleport;
    link.Link.Directions = DockDirectionBoth;
    link.AuthoredEntity = 0x2051;

    const AuthoredLinkCookInput links[] = { link };
    std::string cookError;
    ASSERT_TRUE(CookWorldTopology(manifest, {}, links, &cookError)) << cookError;

    const JsonValue written = WriteWorldPartitionManifest(manifest);
    std::string readError;
    const std::optional<WorldPartitionManifest> reloaded =
        ReadWorldPartitionManifest(written, &readError);
    ASSERT_TRUE(reloaded.has_value()) << readError;

    const WorldPartitionIndex index = WorldPartitionIndex::Build(*reloaded);
    EXPECT_TRUE(Errors(ValidateWorldPartitionManifest(*reloaded, index)).empty());

    ASSERT_EQ(index.LinksFrom(StressZoneAt(0)).size(), 1u);
    ASSERT_EQ(index.LinksFrom(StressZoneAt(2)).size(), 1u);
    EXPECT_EQ(index.LinksFrom(StressZoneAt(0))[0].OtherZone, StressZoneAt(2));
    EXPECT_EQ(index.LinksFrom(StressZoneAt(2))[0].OtherZone, StressZoneAt(0));
    EXPECT_EQ(index.LinksFrom(StressZoneAt(1)).size(), 0u)
        << "a link appeared on a zone it does not touch";
}
