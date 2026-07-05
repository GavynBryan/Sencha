#include <gtest/gtest.h>

#include "document/DocumentSerialization.h"
#include "document/PortalGeometry.h"
#include "document/WorldDocument.h"
#include "document/commands/MoveEntitiesToZoneCommand.h"

#include <core/logging/LoggingProvider.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace
{

// Two open zones (From focused, To context) ready for transition and portal
// checks. Zone bounds are designer-set so tests control the center delta.
class TransitionValidationTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        Root = fs::temp_directory_path()
            / ("sencha_transition_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(Root);
        fs::create_directories(Root);

        World.NewWorld("TestWorld");
        FromZone = World.Manifest().Zones[0].Id;
        ToZone = World.AddZone(World.Manifest().Regions[0].Id, "To");
        ASSERT_TRUE(World.LoadZone(ToZone));
        SetZoneBounds(FromZone, Vec3d{ 0, 0, 0 });
        SetZoneBounds(ToZone, Vec3d{ 40, 0, 0 });
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(Root, ec);
    }

    void SetZoneBounds(ZoneId zone, Vec3d center)
    {
        for (ZoneHeader& header : World.Manifest().Zones)
            if (header.Id == zone)
            {
                header.BoundsOverridden = true;
                header.Bounds = Aabb3d::FromCenterHalfExtent(center, Vec3d{ 10, 10, 10 });
            }
    }

    [[nodiscard]] size_t CountRecords(std::string_view ruleId) const
    {
        size_t count = 0;
        for (const ContentRiskRecord& record : World.ValidationRecords())
            if (record.RuleId == ruleId)
                ++count;
        return count;
    }

    [[nodiscard]] const ContentRiskRecord* FindRecord(std::string_view ruleId) const
    {
        for (const ContentRiskRecord& record : World.ValidationRecords())
            if (record.RuleId == ruleId)
                return &record;
        return nullptr;
    }

    // A thin box brush in the focus zone flagged as a portal. Half extents
    // pick the thin (facing) axis.
    EntityId AddPortalBrush(TransitionId transition, Vec3d halfExtents = { 4.0, 4.0, 0.2 },
                            Vec3d position = { 8, 0, 0 })
    {
        EditorScene& scene = World.FocusDocument().GetScene();
        const EntityId entity = scene.CreateBrush(position, halfExtents);
        scene.GetRegistry().Components.AddComponent(entity, PortalComponent{ transition });
        return entity;
    }

    LoggingProvider Logging;   // sink-less: silent
    fs::path Root;
    WorldDocument World{ Logging };
    ZoneId FromZone;
    ZoneId ToZone;
};

} // namespace

TEST_F(TransitionValidationTest, AddTransitionMintsAndReindexes)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(id.IsValid());
    ASSERT_EQ(World.Index().Outgoing(FromZone).size(), 1u);
    EXPECT_EQ(World.Manifest().Transitions[World.Index().Outgoing(FromZone)[0]].Id, id);
    EXPECT_TRUE(World.IsDirty());
    // Validation ran with the verb: the fresh doorway has no portal yet.
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 1u);
}

TEST_F(TransitionValidationTest, RemoveTransitionDropsRecordAndRevalidates)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);
    ASSERT_EQ(CountRecords("partition.transition.portal_missing"), 1u);

    ASSERT_TRUE(World.RemoveTransition(id));

    EXPECT_TRUE(World.Manifest().Transitions.empty());
    EXPECT_TRUE(World.Index().Outgoing(FromZone).empty());
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);
    EXPECT_FALSE(World.RemoveTransition(id));
}

TEST_F(TransitionValidationTest, SettersRewriteAndRevalidate)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);
    ASSERT_EQ(CountRecords("partition.transition.portal_missing"), 1u);

    // Teleport promises no aperture: the doorway rule clears on the setter.
    ASSERT_TRUE(World.SetTransitionTopology(id, TransitionTopology::Teleport));
    EXPECT_EQ(World.Manifest().Transitions[0].Topology, TransitionTopology::Teleport);
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);

    ASSERT_TRUE(World.SetTransitionOneWay(id, true));
    EXPECT_TRUE(World.Manifest().Transitions[0].Flags.OneWay);

    ASSERT_TRUE(World.SetTransitionPreloadPriority(id, 7));
    EXPECT_EQ(World.Manifest().Transitions[0].PreloadPriority, 7);

    const TransitionId unknown{ 0x00000000000000ffull };
    EXPECT_FALSE(World.SetTransitionTopology(unknown, TransitionTopology::Seam));
    EXPECT_FALSE(World.SetTransitionOneWay(unknown, true));
    EXPECT_FALSE(World.SetTransitionPreloadPriority(unknown, 1));
}

TEST_F(TransitionValidationTest, PortalMissingFires)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    const ContentRiskRecord* record = FindRecord("partition.transition.portal_missing");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Severity, ContentRiskSeverity::Warning);
    EXPECT_EQ(record->Kind, ContentRiskSourceKind::Transition);
    EXPECT_EQ(record->SourceId, id.Value);

    AddPortalBrush(id, Vec3d{ 4.0, 4.0, 0.2 });
    World.Revalidate();
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);
}

TEST_F(TransitionValidationTest, PortalDuplicateFires)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);
    AddPortalBrush(id);
    AddPortalBrush(id, Vec3d{ 4.0, 4.0, 0.2 }, Vec3d{ -8, 0, 0 });
    World.Revalidate();

    const ContentRiskRecord* record = FindRecord("partition.transition.portal_duplicate");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Severity, ContentRiskSeverity::Error);
    EXPECT_EQ(record->SourceId, id.Value);
}

TEST_F(TransitionValidationTest, PortalUnverifiedWhenFromUnloaded)
{
    const ZoneId closed = World.AddZone(World.Manifest().Regions[0].Id, "Closed");
    const TransitionId id =
        World.AddTransition(closed, FromZone, TransitionTopology::Doorway, false, 0);

    const ContentRiskRecord* record = FindRecord("partition.transition.portal_unverified");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Severity, ContentRiskSeverity::Unverified);
    EXPECT_EQ(record->SourceId, id.Value);
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);
}

TEST_F(TransitionValidationTest, PortalMisalignedFires)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);
    // To sits at +X of From, so the portal's thin axis must be X. Thin in Z:
    // the fitted volume faces the wrong wall.
    AddPortalBrush(id, Vec3d{ 4.0, 4.0, 0.2 });
    World.Revalidate();
    ASSERT_EQ(CountRecords("partition.transition.portal_misaligned"), 1u);
    EXPECT_EQ(FindRecord("partition.transition.portal_misaligned")->SourceId, id.Value);

    // Refit thin in X: aligned, the record clears.
    EditorScene& scene = World.FocusDocument().GetScene();
    for (EntityId entity : scene.GetAllEntities())
        scene.SetBrushHalfExtents(entity, Vec3d{ 0.2, 4.0, 4.0 });
    World.Revalidate();
    EXPECT_EQ(CountRecords("partition.transition.portal_misaligned"), 0u);
}

TEST_F(TransitionValidationTest, PortalUnlinkedFires)
{
    AddPortalBrush(TransitionId{});                                // never linked
    AddPortalBrush(TransitionId{ 0x00000000000000ffull },          // names no record
                   Vec3d{ 4.0, 4.0, 0.2 }, Vec3d{ -8, 0, 0 });
    World.Revalidate();

    const ContentRiskRecord* record = FindRecord("partition.portal.unlinked");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Severity, ContentRiskSeverity::Warning);
    EXPECT_EQ(record->Kind, ContentRiskSourceKind::Zone);
    EXPECT_EQ(record->SourceId, FromZone.Value);
    EXPECT_EQ(CountRecords("partition.portal.unlinked"), 1u);
    EXPECT_NE(record->Message.find("2"), std::string::npos);
}

TEST_F(TransitionValidationTest, PortalWrongZoneFires)
{
    // The transition leaves ToZone, but its portal sits in the focus zone
    // (the move-a-portal case).
    const TransitionId id =
        World.AddTransition(ToZone, FromZone, TransitionTopology::Doorway, false, 0);
    AddPortalBrush(id);
    World.Revalidate();

    const ContentRiskRecord* record = FindRecord("partition.portal.wrong_zone");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Severity, ContentRiskSeverity::Error);
    EXPECT_EQ(record->Kind, ContentRiskSourceKind::Transition);
    EXPECT_EQ(record->SourceId, id.Value);
}

TEST_F(TransitionValidationTest, PortalBrushMissingFires)
{
    EditorScene& scene = World.FocusDocument().GetScene();
    const EntityId plain = scene.CreateEntity(Vec3d{ 0, 0, 0 });
    scene.GetRegistry().Components.AddComponent(plain, PortalComponent{});
    World.Revalidate();

    const ContentRiskRecord* record = FindRecord("partition.portal.brush_missing");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->Severity, ContentRiskSeverity::Error);
    EXPECT_EQ(record->Kind, ContentRiskSourceKind::Zone);
    EXPECT_EQ(record->SourceId, FromZone.Value);
}

TEST_F(TransitionValidationTest, TeleportSkipsPortalRules)
{
    World.AddTransition(FromZone, ToZone, TransitionTopology::Teleport, false, 0);
    const ZoneId closed = World.AddZone(World.Manifest().Regions[0].Id, "Closed");
    World.AddTransition(closed, FromZone, TransitionTopology::Seam, false, 0);

    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);
    EXPECT_EQ(CountRecords("partition.transition.portal_unverified"), 0u);
    EXPECT_EQ(CountRecords("partition.transition.portal_misaligned"), 0u);
}

TEST(DominantPortalAxisTest, DominantAxisIsMinimumExtent)
{
    EXPECT_EQ(DominantPortalAxis(
                  Aabb3d::FromCenterHalfExtent(Vec3d{ 0, 0, 0 }, Vec3d{ 0.1, 4, 4 })),
              0);
    EXPECT_EQ(DominantPortalAxis(
                  Aabb3d::FromCenterHalfExtent(Vec3d{ 5, 1, 2 }, Vec3d{ 4, 0.1, 4 })),
              1);
    EXPECT_EQ(DominantPortalAxis(
                  Aabb3d::FromCenterHalfExtent(Vec3d{ -3, 0, 7 }, Vec3d{ 4, 4, 0.1 })),
              2);
}

TEST_F(TransitionValidationTest, RenameTransitionRewritesAndRevalidates)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(World.RenameTransition(id, "Front Door"));
    EXPECT_EQ(World.Manifest().Transitions[0].Name, "Front Door");
    EXPECT_TRUE(World.IsDirty());

    // Clearing restores the derived display label (empty stored name).
    ASSERT_TRUE(World.RenameTransition(id, ""));
    EXPECT_TRUE(World.Manifest().Transitions[0].Name.empty());
    EXPECT_FALSE(World.RenameTransition(TransitionId{ 0xff }, "x"));
}

TEST_F(TransitionValidationTest, PortalPairSatisfiedByOneSide)
{
    // A symmetric doorway pair with one portal, linked to the forward edge,
    // living in the From zone: both directions' aperture checks clear even
    // though the reverse edge's own zone holds no portal.
    const TransitionId forward =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);
    (void)World.AddTransition(ToZone, FromZone, TransitionTopology::Doorway, false, 0);
    AddPortalBrush(forward, Vec3d{ 0.2, 4.0, 4.0 });
    World.Revalidate();

    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);
    EXPECT_EQ(CountRecords("partition.transition.portal_unverified"), 0u);
}

TEST_F(TransitionValidationTest, OneWayStillRequiresOwnPortal)
{
    // Two OneWay edges are not a symmetric pair: each direction keeps
    // demanding its own portal.
    const TransitionId forward =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, true, 0);
    (void)World.AddTransition(ToZone, FromZone, TransitionTopology::Doorway, true, 0);
    AddPortalBrush(forward, Vec3d{ 0.2, 4.0, 4.0 });
    World.Revalidate();

    // The reverse OneWay edge's zone is open (fixture loads it) and unportaled.
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 1u);
}

TEST_F(TransitionValidationTest, TagAndDepthVerbsRewriteRecords)
{
    const TransitionId id =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 0);

    ASSERT_TRUE(World.SetTransitionRequiredTags(id, { "quest.bridge", "power.on" }));
    ASSERT_EQ(World.Manifest().Transitions[0].RequiredTags.size(), 2u);
    EXPECT_EQ(World.Manifest().Transitions[0].RequiredTags[0], "quest.bridge");

    ASSERT_TRUE(World.SetTransitionPreloadDepth(id, 3));
    EXPECT_EQ(World.Manifest().Transitions[0].PreloadDepth, 3);
    ASSERT_TRUE(World.SetTransitionPreloadDepth(id, -5));
    EXPECT_EQ(World.Manifest().Transitions[0].PreloadDepth, 0);   // clamped

    const TransitionId unknown{ 0xff };
    EXPECT_FALSE(World.SetTransitionRequiredTags(unknown, {}));
    EXPECT_FALSE(World.SetTransitionPreloadDepth(unknown, 1));
}

TEST_F(TransitionValidationTest, PortalPlacementDerivesConnection)
{
    // A portal straddling the boundary between the zones (bounds [-10..10] and
    // [30..50] in X, portal at the From zone's +X face): the counterpart
    // derives from geometry, the doorway pair mints itself, and the portal
    // links its own zone's edge. No linking step anywhere.
    SetZoneBounds(ToZone, Vec3d{ 12, 0, 0 });   // adjacent: [2..22] in X
    const EntityId portal = AddPortalBrush(TransitionId{}, Vec3d{ 0.2, 4.0, 4.0 },
                                           Vec3d{ 10, 0, 0 });
    World.Revalidate();

    ASSERT_EQ(World.Manifest().Transitions.size(), 2u);
    const PortalComponent* component =
        World.FocusDocument().GetScene().TryGetPortal(portal);
    ASSERT_TRUE(component->Transition.IsValid());
    const TransitionRecord& forward = World.Manifest().Transitions[0];
    EXPECT_EQ(component->Transition, forward.Id);
    EXPECT_EQ(forward.From, FromZone);
    EXPECT_EQ(forward.To, ToZone);
    EXPECT_EQ(World.Manifest().Transitions[1].From, ToZone);
    EXPECT_EQ(World.Manifest().Transitions[1].To, FromZone);
    // The derived pair satisfies validation outright.
    EXPECT_EQ(CountRecords("partition.transition.portal_missing"), 0u);
    EXPECT_EQ(CountRecords("partition.portal.unlinked"), 0u);
}

TEST_F(TransitionValidationTest, ReconcileReusesExistingPairAndNeverRemoves)
{
    SetZoneBounds(ToZone, Vec3d{ 12, 0, 0 });
    const TransitionId existing =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Doorway, false, 7);
    (void)World.AddTransition(ToZone, FromZone, TransitionTopology::Doorway, false, 7);
    // An unrelated teleport must survive reconciliation untouched.
    const TransitionId teleport =
        World.AddTransition(FromZone, ToZone, TransitionTopology::Teleport, true, 0);

    const EntityId portal = AddPortalBrush(TransitionId{}, Vec3d{ 0.2, 4.0, 4.0 },
                                           Vec3d{ 10, 0, 0 });
    World.Revalidate();

    // Linked to the EXISTING doorway edge; nothing new minted, nothing removed.
    EXPECT_EQ(World.FocusDocument().GetScene().TryGetPortal(portal)->Transition, existing);
    ASSERT_EQ(World.Manifest().Transitions.size(), 3u);
    bool teleportSurvives = false;
    for (const TransitionRecord& record : World.Manifest().Transitions)
        teleportSurvives |= record.Id == teleport;
    EXPECT_TRUE(teleportSurvives);
}

TEST_F(TransitionValidationTest, MovedPortalRelinksToItsNewZone)
{
    SetZoneBounds(ToZone, Vec3d{ 12, 0, 0 });
    const EntityId portal = AddPortalBrush(TransitionId{}, Vec3d{ 0.2, 4.0, 4.0 },
                                           Vec3d{ 10, 0, 0 });
    World.Revalidate();
    const TransitionId original =
        World.FocusDocument().GetScene().TryGetPortal(portal)->Transition;
    ASSERT_TRUE(original.IsValid());

    // The E2 move carries the portal into the other zone; on the next
    // reconcile it links THAT zone's edge of the pair instead.
    const EntityId entities[] = { portal };
    MoveEntitiesToZoneCommand move(entities, World.FocusDocument(),
                                   *World.ZoneDocument(ToZone));
    move.Execute();
    World.Revalidate();

    const EditorScene& targetScene = World.ZoneDocument(ToZone)->GetScene();
    const EntityId moved = targetScene.GetAllEntities()[0];
    const TransitionRecord* linked = nullptr;
    for (const TransitionRecord& record : World.Manifest().Transitions)
        if (record.Id == targetScene.TryGetPortal(moved)->Transition)
            linked = &record;
    ASSERT_NE(linked, nullptr);
    EXPECT_EQ(linked->From, ToZone);
    EXPECT_NE(linked->Id, original);
    ASSERT_EQ(World.Manifest().Transitions.size(), 2u);   // the pair, reused
}
