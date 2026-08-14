// Utility generator: cooks a three-room chain into a chosen assets root.
//
// The world the networking track's traversal gate needs and the template does
// not ship. Three rooms in a line, each with a floor, joined by two doorways --
// enough for two players to walk from one end to the other and for zones to
// enter and leave residency behind them, and nothing else.
//
// A generator rather than checked-in JSON written by hand, because the cook is
// what decides the cooked layout, the content hashes, and the dock endpoints
// authored transforms compile to. Hand-written artifacts would be a second
// answer to all three, correct until the day the cook changed.
//
// Skipped unless SENCHA_TRAVERSAL_ROOT (the assets root to cook into) is set:
//
//   SENCHA_TRAVERSAL_ROOT=template/assets
//     ./build/test/level_cook_tests --gtest_filter=TraversalWorld.Generate

#include "document/DocumentSerialization.h"
#include "document/WorldCook.h"
#include "document/WorldDocument.h"

#include <core/logging/LoggingProvider.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    // Rooms run along X, meeting at their shared walls. Sized so a player
    // crossing one takes long enough that a load has somewhere to happen.
    constexpr double kRoomHalfWidth = 8.0;
    constexpr double kRoomHalfDepth = 8.0;
    constexpr double kRoomHeight = 6.0;
    constexpr double kFloorThickness = 0.25;
    constexpr int kRoomCount = 3;

    // Room `index` runs from its left wall to its right one.
    double RoomCenterX(int index)
    {
        return static_cast<double>(index) * kRoomHalfWidth * 2.0;
    }
    // The wall room `index` shares with the next one along.
    double DoorwayX(int index)
    {
        return RoomCenterX(index) + kRoomHalfWidth;
    }

    Aabb3d RoomBounds(int index)
    {
        const double centerX = RoomCenterX(index);
        return Aabb3d::FromMinMax(
            Vec3d{ static_cast<float>(centerX - kRoomHalfWidth), 0.0f,
                   static_cast<float>(-kRoomHalfDepth) },
            Vec3d{ static_cast<float>(centerX + kRoomHalfWidth),
                   static_cast<float>(kRoomHeight),
                   static_cast<float>(kRoomHalfDepth) });
    }
}

TEST(TraversalWorld, Generate)
{
    const char* root = std::getenv("SENCHA_TRAVERSAL_ROOT");
    if (root == nullptr)
        GTEST_SKIP() << "set SENCHA_TRAVERSAL_ROOT to cook the traversal world";

    RegisterDocumentSerializers();
    LoggingProvider logging;

    const fs::path assetsRoot = fs::absolute(root);
    // At the assets root, not under levels/: the world file is the anchor its
    // zone scenes are written beside, so one directory deeper nests them.
    const fs::path authored = assetsRoot / "traversal3.sworld";
    fs::create_directories(assetsRoot / "levels");

    WorldDocument world(logging);
    world.NewWorld("Traversal3");

    const GraphId graph = world.Manifest().Graphs[0].Id;
    std::vector<ZoneId> rooms{ world.Manifest().Zones[0].Id };
    for (int index = 1; index < kRoomCount; ++index)
        rooms.push_back(world.AddZone(graph, "Room " + std::to_string(index + 1)));

    // A floor per room, filling it, with its top face at y = 0 so a player
    // stands where the zone's bounds say they are.
    for (int index = 0; index < kRoomCount; ++index)
    {
        ASSERT_TRUE(world.SetZoneBounds(rooms[index], RoomBounds(index)));
        ASSERT_TRUE(world.SetFocusZone(rooms[index]));
        world.FocusDocument().GetScene().CreateBrush(
            Vec3d{ static_cast<float>(RoomCenterX(index)),
                   static_cast<float>(-kFloorThickness), 0.0f },
            Vec3d{ static_cast<float>(kRoomHalfWidth),
                   static_cast<float>(kFloorThickness),
                   static_cast<float>(kRoomHalfDepth) });
    }

    // Doorways, authored in the world scene as the cook reads them. The plane's
    // normal is minus its forward, and forward is -Z, so a quarter turn about Y
    // stands each doorway across the corridor rather than along it.
    EditorDocument& worldScene = world.WorldSceneDocument();
    const Quatf facingX = Quatf::FromAxisAngle(
        Vec3d{ 0.0f, 1.0f, 0.0f }, std::numbers::pi_v<float> / 2.0f);

    for (int index = 0; index + 1 < kRoomCount; ++index)
    {
        const EntityId doorway = worldScene.GetScene().CreateEntity(
            Vec3d{ static_cast<float>(DoorwayX(index)), 1.5f, 0.0f });
        if (LocalTransform* transform = worldScene.GetScene()
                .GetRegistry()
                .Components.TryGet<LocalTransform>(doorway))
        {
            transform->Value.Rotation = facingX;
        }
        worldScene.GetScene().GetRegistry().Components.AddComponent(
            doorway,
            WorldDock{
                .Id = world.MintDockId(),
                .ZoneA = rooms[index],
                .ZoneB = rooms[index + 1],
                .HalfExtents = Vec2d{ 3.0f, 1.5f },
                .Directions = DockDirectionBoth,
            });
    }

    ASSERT_TRUE(world.SaveWorldAs(authored.string()));

    const WorldCookResult cooked =
        CookWorld(world, assetsRoot, 16.0, logging, nullptr);
    ASSERT_TRUE(cooked.Success) << cooked.Error;
    EXPECT_EQ(cooked.ZoneCount, static_cast<std::size_t>(kRoomCount));

    std::printf("cooked '%s': zones=%zu manifest=%s\n",
                authored.generic_string().c_str(), cooked.ZoneCount,
                cooked.CookedManifestPath.generic_string().c_str());
}
