// Spaces proposed from walkable surface: deep floor is a room, the narrow
// surface left between rooms a passage, and floors at different heights apart.

#include "operations/SpaceProposals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <vector>

namespace
{
    // Walkable rectangles at a height, as navigation polygons.
    struct Surface
    {
        NavMeshSnapshot Snapshot;

        Surface& Add(float minX, float minZ, float maxX, float maxZ, float y = 0.0f)
        {
            NavSnapshotPolygon polygon;
            polygon.FirstVertex = static_cast<std::uint32_t>(Snapshot.Vertices.size());
            polygon.VertexCount = 4;
            Snapshot.Vertices.push_back({ minX, y, minZ });
            Snapshot.Vertices.push_back({ maxX, y, minZ });
            Snapshot.Vertices.push_back({ maxX, y, maxZ });
            Snapshot.Vertices.push_back({ minX, y, maxZ });
            Snapshot.Polygons.push_back(polygon);
            return *this;
        }

        std::vector<ProposedSpace> Propose(SpaceProposalSettings settings = {}) const
        {
            std::vector<std::uint32_t> all(Snapshot.Polygons.size());
            std::iota(all.begin(), all.end(), 0u);
            return ProposeSpaces(Snapshot, all, settings);
        }
    };

    std::size_t Count(const std::vector<ProposedSpace>& spaces, ProposedSpaceKind kind)
    {
        return static_cast<std::size_t>(std::ranges::count(spaces, kind, &ProposedSpace::Kind));
    }
}

TEST(SpaceProposals, OneFloorIsOneRoomReachingTheWallsBehindItsEdge)
{
    const std::vector<ProposedSpace> spaces = Surface{}.Add(0, 0, 8, 6).Propose();
    ASSERT_EQ(spaces.size(), 1u);
    EXPECT_EQ(spaces[0].Kind, ProposedSpaceKind::Room);
    EXPECT_NEAR(spaces[0].Min.X, -0.3f, 1e-3f) << "walkable surface stops the inset short of the wall";
    EXPECT_NEAR(spaces[0].Max.Z, 6.3f, 1e-3f);
    EXPECT_NEAR(spaces[0].Area, 48.0f, 0.5f);
}

TEST(SpaceProposals, ANarrowCorridorBetweenRoomsIsAPassage)
{
    const std::vector<ProposedSpace> spaces =
        Surface{}.Add(0, 0, 8, 8).Add(8, 3.25f, 12, 4.75f).Add(12, 0, 20, 8).Propose();
    EXPECT_EQ(Count(spaces, ProposedSpaceKind::Room), 2u);
    ASSERT_EQ(Count(spaces, ProposedSpaceKind::Passage), 1u);
    const ProposedSpace& passage = spaces.back();
    EXPECT_GE(passage.Min.X, 7.7f) << "the passage lies along the corridor between the rooms";
    EXPECT_LE(passage.Max.X, 12.3f);
    EXPECT_GT(passage.Max.X - passage.Min.X, 3.0f) << "and is most of it";
    EXPECT_LT(passage.Max.Z - passage.Min.Z, 2.5f);
}

TEST(SpaceProposals, AWideOpeningJoinsTwoFloorsIntoOneRoom)
{
    const std::vector<ProposedSpace> spaces = Surface{}.Add(0, 0, 8, 8).Add(8, 2, 12, 6).Add(12, 0, 20, 8).Propose();
    EXPECT_EQ(Count(spaces, ProposedSpaceKind::Room), 1u) << "a 4 m wide way through is still the room";
}

TEST(SpaceProposals, FloorsAtDifferentHeightsAreDifferentRooms)
{
    const std::vector<ProposedSpace> spaces = Surface{}.Add(0, 0, 8, 8, 0.0f).Add(0, 0, 8, 8, 4.0f).Propose();
    ASSERT_EQ(Count(spaces, ProposedSpaceKind::Room), 2u);
    EXPECT_NE(spaces[0].Min.Y, spaces[1].Min.Y);
}

TEST(SpaceProposals, OnlyTheListedPolygonsAreRead)
{
    Surface surface;
    surface.Add(0, 0, 8, 8).Add(20, 0, 28, 8);
    const std::vector<std::uint32_t> second{ 1 };
    const std::vector<ProposedSpace> spaces = ProposeSpaces(surface.Snapshot, second, {});
    ASSERT_EQ(spaces.size(), 1u);
    EXPECT_GT(spaces[0].Min.X, 19.0f);
}
