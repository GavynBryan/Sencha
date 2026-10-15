// world.reachable and world.bypasses: a zone is left only through a dock, a
// dock in a floor is a one-way drop, a gate shuts its dock for the sure walk,
// and the template world's walk turns on an authored jump link.

#include "NavigationSettingsFixture.h"

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/WorldDocument.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <navigation/NavLinkComponent.h>
#include <world/registry/Registry.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    class WorldReachabilityTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            Root = fs::temp_directory_path()
                / ("sencha_world_reach_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
            fs::remove_all(Root);
            WriteTestNavigationSettings(Root);

            World.NewWorld("Tree");
            Upper = World.Manifest().Zones[0].Id;
            Basement = World.AddZone(World.Manifest().Graphs[0].Id, "Basement");
            ASSERT_TRUE(World.SetZoneBounds(Upper, Aabb3d(Vec3d{ -8, 0, -8 }, Vec3d{ 8, 6, 8 })));
            ASSERT_TRUE(World.SetZoneBounds(Basement, Aabb3d(Vec3d{ -8, -10, -8 }, Vec3d{ 8, 0, 8 })));
            ASSERT_TRUE(World.LoadZone(Basement));
            World.SetContentRoots({ Root });

            // The upper floor, its top at y = 0, with holes at x 0 and x 5.
            EditorScene& upper = World.ZoneDocument(Upper)->GetScene();
            (void)upper.CreateBrush(Vec3d{ 0, -0.5f, -4.5f }, Vec3d{ 8, 0.5f, 3.5f });
            (void)upper.CreateBrush(Vec3d{ 0, -0.5f, 4.5f }, Vec3d{ 8, 0.5f, 3.5f });
            (void)upper.CreateBrush(Vec3d{ -4.5f, -0.5f, 0 }, Vec3d{ 3.5f, 0.5f, 1 });
            (void)upper.CreateBrush(Vec3d{ 2.5f, -0.5f, 0 }, Vec3d{ 1.5f, 0.5f, 1 });
            (void)upper.CreateBrush(Vec3d{ 7, -0.5f, 0 }, Vec3d{ 1, 0.5f, 1 });
            // The basement floor, its top at y = -8.
            (void)World.ZoneDocument(Basement)->GetScene().CreateBrush(Vec3d{ 0, -8.5f, 0 }, Vec3d{ 8, 0.5f, 8 });

            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }

        // A one-way dock lying in the floor over the hole at `x`, crossed downward.
        DockId Drop(float x)
        {
            EditorScene& scene = World.WorldSceneDocument().GetScene();
            const EntityId entity = scene.CreateEntity(Vec3d{ x, 0, 0 });
            Transform3f frame = Transform3f::Identity();
            frame.Position = Vec3d{ x, 0, 0 };
            frame.Rotation = Quatf::FromAxisAngle(Vec3d{ 1, 0, 0 }, 0.5f * std::numbers::pi_v<float>);
            scene.SetWorldTransform(entity, frame);
            WorldDock dock;
            dock.Id = World.MintDockId();
            dock.ZoneA = Upper;
            dock.ZoneB = Basement;
            dock.HalfExtents = Vec2d{ 1, 1 };
            dock.Directions = DockDirectionAToB;
            scene.GetRegistry().Components.AddComponent(entity, dock);
            scene.RefreshDerivedTransforms();
            return dock.Id;
        }

        void Gate(DockId dock)
        {
            EditorScene& scene = World.ZoneDocument(Upper)->GetScene();
            const EntityId web = scene.CreateEntity(Vec3d{ 0, 0.5f, 0 });
            scene.GetRegistry().Components.AddComponent(web, DockGateBinding{ dock });
        }

        JsonValue Ok(std::string_view name, std::string_view arguments)
        {
            const JsonValue answer = Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
            EXPECT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
            const JsonValue* data = answer.Find("data");
            return data != nullptr ? *data : JsonValue{};
        }

        static std::string Outcome(const JsonValue& data) { return data.Find("outcome")->AsString(); }

        fs::path Root;
        LoggingProvider Logging;
        WorldDocument World{ Logging, WorldDocument::UserState::Ignored };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
        ZoneId Upper;
        ZoneId Basement;
    };

    constexpr std::string_view kDown = R"({"from": [-5, 0, 5], "to": [-5, -8, -5])";
    constexpr std::string_view kUp = R"({"from": [-5, -8, -5], "to": [-5, 0, 5]})";

    TEST_F(WorldReachabilityTest, OnlyADockLeadsFromOneZoneToAnother)
    {
        EXPECT_EQ(Outcome(Ok("world.reachable", std::string(kDown) + "}")), "unreached")
            << "an open hole is no way down until a dock says it is";
    }

    TEST_F(WorldReachabilityTest, ADockInTheFloorIsAOneWayDrop)
    {
        const DockId hole = Drop(0);
        const JsonValue down = Ok("world.reachable", std::string(kDown) + "}");
        EXPECT_EQ(Outcome(down), "reached");
        ASSERT_EQ(down.Find("docks")->AsArray().size(), 1u);
        EXPECT_EQ(down.Find("docks")->AsArray()[0].AsString(), DockIdToString(hole));
        EXPECT_EQ(Outcome(Ok("world.reachable", kUp)), "unreached") << "nothing leads back up";
    }

    TEST_F(WorldReachabilityTest, AGatedDropDependsOnItsGate)
    {
        const DockId hole = Drop(0);
        Gate(hole);
        const JsonValue shut = Ok("world.reachable", std::string(kDown) + "}");
        EXPECT_EQ(Outcome(shut), "depends_on");
        ASSERT_EQ(shut.Find("depends_on_gates")->AsArray().size(), 1u);
        EXPECT_EQ(shut.Find("depends_on_gates")->AsArray()[0].AsString(), DockIdToString(hole));

        EXPECT_EQ(Outcome(Ok("world.reachable", std::string(kDown) + R"(, "open_gates": [")" + DockIdToString(hole) + R"("]})")),
                  "reached");
        EXPECT_EQ(Outcome(Ok("world.reachable", std::string(kDown) + R"(, "closed_gates": [")" + DockIdToString(hole) + R"("]})")),
                  "unreached");
        EXPECT_TRUE(Ok("world.bypasses", R"({"from": [-5, 0, 5]})").Find("bypasses")->AsArray().empty());
    }

    TEST_F(WorldReachabilityTest, ASecondWayDownBypassesTheGate)
    {
        const DockId gated = Drop(0);
        Gate(gated);
        (void)Drop(5);
        const JsonValue bypasses = Ok("world.bypasses", R"({"from": [-5, 0, 5]})");
        ASSERT_EQ(bypasses.Find("bypasses")->AsArray().size(), 1u) << JsonStringify(bypasses);
        EXPECT_EQ(bypasses.Find("bypasses")->AsArray()[0].Find("dock")->AsString(), DockIdToString(gated));
        EXPECT_EQ(Outcome(Ok("world.reachable", std::string(kDown) + "}")), "reached");
    }
}

namespace
{
    // The template world, read from the repository and changed only in memory:
    // a wall across Room 2 that only an authored jump link gets over.
    class Traversal3JumpTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        void SetUp() override
        {
            World.SetContentRoots({ std::string(SENCHA_REPO_ROOT) + "/templates/fps/assets" });
            ASSERT_TRUE(World.LoadWorld(std::string(SENCHA_REPO_ROOT) + "/templates/fps/assets/traversal3.sworld"));
            const ZoneId room2 = World.Manifest().Zones[1].Id;
            ASSERT_TRUE(World.LoadZone(room2));
            EditorScene& scene = World.ZoneDocument(room2)->GetScene();
            (void)scene.CreateBrush(Vec3d{ 16, 3, 0 }, Vec3d{ 0.5f, 3, 8 });
            const Vec3d entry(14.0f, 0.0f, 0.0f);
            const EntityId jump = scene.CreateEntity(entry);
            NavLink link;
            link.Id = NavLinkId{ 0x7a3 };
            link.Traversal = NavTraversalName("navigation.traversal.jump");
            link.ExitOffset = Vec3d(18.0f, 0.0f, 0.0f) - entry;
            scene.GetRegistry().Components.AddComponent(jump, link);
            std::vector<std::string> errors;
            ASSERT_TRUE(Operations.Register("level", LevelOperations(Level, Operations), errors));
        }

        JsonValue Ask(std::string_view scenario)
        {
            const JsonValue answer = Operations.Invoke("world.reachable",
                JsonParse(R"({"from": [0, 0, 0], "to": [32, 0, 0])" + std::string(scenario) + "}").value_or(JsonValue{}));
            EXPECT_EQ(answer.Find("status")->AsString(), "ok") << JsonStringify(answer);
            return *answer.Find("data");
        }

        // What each zone's leg crosses, "zone:kind".
        static std::vector<std::string> Crossings(const JsonValue& data)
        {
            std::vector<std::string> out;
            for (const JsonValue& leg : data.Find("legs")->AsArray())
                for (const JsonValue& kind : leg.Find("crossed")->AsArray())
                    out.push_back(leg.Find("zone")->Find("name")->AsString() + ":" + kind.AsString());
            return out;
        }

        LoggingProvider Logging;
        WorldDocument World{ Logging, WorldDocument::UserState::Ignored };
        CommandStack Commands;
        LevelTarget Level{ World, Commands };
        EditorOperations Operations;
    };

    TEST_F(Traversal3JumpTest, TheJumpScenarioDecidesTheWalkAtTheAuthoredLink)
    {
        const std::vector<std::string> atTheLink{ "Room 2:navigation.traversal.jump" };
        const JsonValue unknown = Ask("");
        EXPECT_EQ(unknown.Find("outcome")->AsString(), "depends_on");
        ASSERT_EQ(unknown.Find("depends_on")->AsArray().size(), 1u);
        EXPECT_EQ(unknown.Find("depends_on")->AsArray()[0].AsString(), "navigation.traversal.jump");
        EXPECT_EQ(Crossings(unknown), atTheLink) << "the possible walk names where it jumps";

        const JsonValue held = Ask(R"(, "held": ["navigation.traversal.jump"])");
        EXPECT_EQ(held.Find("outcome")->AsString(), "reached");
        EXPECT_EQ(Crossings(held), atTheLink) << "and so does the sure walk";

        EXPECT_EQ(Ask(R"(, "denied": ["navigation.traversal.jump"])").Find("outcome")->AsString(), "unreached");
    }
}
