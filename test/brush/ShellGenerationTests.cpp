// Shell generation: one closed slab per volume face, bounded by the join rules,
// and a refusal (never an approximation) for a volume it cannot wall.

#include "brush/BrushMesh.h"
#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/ShellGeneration.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace
{
    constexpr float kTol = 1e-4f;

    // A vertical prism over a counter-clockwise (seen from above) footprint in
    // the XZ plane, from y0 to y1.
    BrushMesh Prism(const std::vector<Vec2d>& footprint, float y0, float y1)
    {
        BrushMesh mesh;
        const std::uint32_t n = static_cast<std::uint32_t>(footprint.size());
        for (const Vec2d& p : footprint)
            mesh.Vertices.push_back({ Vec3d{ p.X, y0, p.Y } });
        for (const Vec2d& p : footprint)
            mesh.Vertices.push_back({ Vec3d{ p.X, y1, p.Y } });
        BrushFace bottom, top;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            bottom.Loop.push_back(i);
            top.Loop.push_back(n + i);
        }
        mesh.Faces.push_back(bottom);
        mesh.Faces.push_back(top);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            const std::uint32_t j = (i + 1) % n;
            mesh.Faces.push_back(BrushFace{ { i, j, n + j, n + i } });
        }
        BrushOrientFacesOutward(mesh);
        return mesh;
    }

    const ShellSlab& SlabOn(const ShellResult& shell, const BrushMesh& volume, Vec3d normal)
    {
        for (const ShellSlab& slab : shell.Slabs)
            if (BrushComputeFaceNormal(volume, volume.Faces[slab.SourceFace]).Dot(normal) > 0.99f)
                return slab;
        ADD_FAILURE() << "no slab on that face";
        return shell.Slabs.front();
    }

    void ExpectBounds(const BrushMesh& mesh, Vec3d min, Vec3d max)
    {
        const Aabb3d bounds = BrushComputeBounds(mesh);
        EXPECT_NEAR(bounds.Min.X, min.X, kTol); EXPECT_NEAR(bounds.Min.Y, min.Y, kTol); EXPECT_NEAR(bounds.Min.Z, min.Z, kTol);
        EXPECT_NEAR(bounds.Max.X, max.X, kTol); EXPECT_NEAR(bounds.Max.Y, max.Y, kTol); EXPECT_NEAR(bounds.Max.Z, max.Z, kTol);
    }

    bool Closed(BrushMesh mesh)
    {
        return BrushValidateAndRepair(mesh).Closed;
    }

    const AssetRef kOpen{ AssetType::Material, "asset://materials/dev/open.smat" };

    ShellRules Rules(float thickness, AssetRef openFace = {})
    {
        ShellRules rules;
        rules.Thickness = thickness;
        rules.OpenFaceMaterial = std::move(openFace);
        return rules;
    }
}

TEST(ShellGeneration, ABoxRoomGetsSixClosedSlabsJoinedAtItsCorners)
{
    const BrushMesh room = BrushOps::MakeBox({ 4, 1.5f, 3 });
    const ShellResult shell = GenerateShell(room, Rules(0.25f));
    ASSERT_FALSE(shell.Refused);
    ASSERT_EQ(shell.Slabs.size(), 6u);
    for (const ShellSlab& slab : shell.Slabs)
    {
        EXPECT_TRUE(Closed(slab.Mesh));
        ASSERT_EQ(slab.InnerFaces.size(), 1u);
        EXPECT_LT(slab.Mesh.Faces[slab.InnerFaces.front()].Normal.Dot(room.Faces[slab.SourceFace].Normal), -0.99f)
            << "the inner face looks back into the room";
    }

    // Floors and ceilings run under and over the walls; walls meet each other
    // at bisectors and stop on the floor and ceiling planes.
    ExpectBounds(SlabOn(shell, room, { 0, -1, 0 }).Mesh, { -4.25f, -1.75f, -3.25f }, { 4.25f, -1.5f, 3.25f });
    ExpectBounds(SlabOn(shell, room, { 0, 1, 0 }).Mesh, { -4.25f, 1.5f, -3.25f }, { 4.25f, 1.75f, 3.25f });
    ExpectBounds(SlabOn(shell, room, { 1, 0, 0 }).Mesh, { 4, -1.5f, -3.25f }, { 4.25f, 1.5f, 3.25f });
    ExpectBounds(SlabOn(shell, room, { 0, 0, -1 }).Mesh, { -4.25f, -1.5f, -3.25f }, { 4.25f, 1.5f, -3 });
}

TEST(ShellGeneration, AWallsInnerAndOuterFacesShareTheirBottomEdge)
{
    const BrushMesh room = BrushOps::MakeBox({ 4, 1.5f, 3 });
    const ShellResult shell = GenerateShell(room, Rules(0.25f));
    const BrushMesh& wall = SlabOn(shell, room, { 1, 0, 0 }).Mesh;
    int onFloor = 0;
    for (const BrushVertex& vertex : wall.Vertices)
        if (std::abs(vertex.Position.Y + 1.5f) < kTol)
            ++onFloor;
    EXPECT_EQ(onFloor, 4) << "a doorway cut at floor level must become a notch, not a window";
}

TEST(ShellGeneration, AnOpenFaceGetsNoSlabAndItsNeighboursStopOnItsPlane)
{
    BrushMesh room = BrushOps::MakeBox({ 4, 1.5f, 3 });
    for (BrushFace& face : room.Faces)
        if (BrushComputeFaceNormal(room, face).Y > 0.99f)
            face.Material.Material = kOpen;
    const ShellResult shell = GenerateShell(room, Rules(0.25f, kOpen));
    ASSERT_FALSE(shell.Refused);
    ASSERT_EQ(shell.Slabs.size(), 5u);
    ExpectBounds(SlabOn(shell, room, { 1, 0, 0 }).Mesh, { 4, -1.5f, -3.25f }, { 4.25f, 1.5f, 3.25f });
}

TEST(ShellGeneration, AnLShapedRoomIsWalledAroundItsInsideCorner)
{
    const BrushMesh room = Prism({ { 0, 0 }, { 0, 6 }, { 3, 6 }, { 3, 3 }, { 8, 3 }, { 8, 0 } }, 0, 3);
    const ShellResult shell = GenerateShell(room, Rules(0.25f));
    ASSERT_FALSE(shell.Refused) << shell.Problems.front().Message;
    ASSERT_EQ(shell.Slabs.size(), 8u);
    for (const ShellSlab& slab : shell.Slabs)
        EXPECT_TRUE(Closed(slab.Mesh));
    ExpectBounds(SlabOn(shell, room, { 0, -1, 0 }).Mesh, { -0.25f, -0.25f, -0.25f }, { 8.25f, 0, 6.25f });
}

TEST(ShellGeneration, ASlotNarrowerThanTheWallsIsRefused)
{
    const BrushMesh room = Prism({ { 0, 0 }, { 4, 0 }, { 4, 2.95f }, { 3, 2.95f }, { 3, 3.05f }, { 4, 3.05f },
                                   { 4, 6 }, { 0, 6 } }, 0, 3);
    const ShellResult shell = GenerateShell(room, Rules(0.25f));
    EXPECT_TRUE(shell.Refused);
    EXPECT_TRUE(shell.Slabs.empty());
    ASSERT_FALSE(shell.Problems.empty());
    EXPECT_EQ(shell.Problems.front().Kind, ShellProblemKind::InvertedSlab);
}

TEST(ShellGeneration, AnOpenOrBentVolumeIsRefused)
{
    const ShellResult open = GenerateShell(BrushOps::MakePlane({ 2, 0, 2 }, 1), ShellRules{});
    EXPECT_TRUE(open.Refused);
    EXPECT_EQ(open.Problems.front().Kind, ShellProblemKind::OpenVolume);

    BrushMesh bent = BrushOps::MakeBox({ 2, 2, 2 });
    bent.Vertices.front().Position.Y += 0.3f;
    const ShellResult refused = GenerateShell(bent, ShellRules{});
    EXPECT_TRUE(refused.Refused);
    EXPECT_EQ(refused.Problems.front().Kind, ShellProblemKind::NonPlanarFace);
}

TEST(ShellGeneration, FourPlanesAtACornerAreNotedNotRefused)
{
    BrushMesh pyramid;
    pyramid.Vertices = { { { -2, 0, -2 } }, { { 2, 0, -2 } }, { { 2, 0, 2 } }, { { -2, 0, 2 } }, { { 0, 3, 0 } } };
    pyramid.Faces = { BrushFace{ { 0, 1, 2, 3 } }, BrushFace{ { 0, 4, 1 } }, BrushFace{ { 1, 4, 2 } },
                      BrushFace{ { 2, 4, 3 } }, BrushFace{ { 3, 4, 0 } } };
    BrushOrientFacesOutward(pyramid);
    const ShellResult shell = GenerateShell(pyramid, Rules(0.2f));
    EXPECT_FALSE(shell.Refused);
    EXPECT_EQ(shell.Slabs.size(), 5u);
    ASSERT_EQ(shell.Problems.size(), 1u);
    EXPECT_EQ(shell.Problems.front().Kind, ShellProblemKind::VertexJoin);
    EXPECT_EQ(shell.Problems.front().Element, 4u);
}

namespace
{
    float FacingArea(const BrushMesh& mesh, Vec3d normal)
    {
        float area = 0.0f;
        for (const BrushFace& face : mesh.Faces)
        {
            if (face.Normal.Dot(normal) < 0.99f)
                continue;
            Vec3d sum{ 0, 0, 0 };
            for (std::size_t i = 0; i < face.Loop.size(); ++i)
                sum = sum + mesh.Vertices[face.Loop[i]].Position.Cross(
                                mesh.Vertices[face.Loop[(i + 1) % face.Loop.size()]].Position);
            area += 0.5f * std::abs(sum.Dot(normal));
        }
        return area;
    }

    const AssetRef kOther{ AssetType::Material, "asset://materials/dev/red.smat" };

    BrushMesh Painted(BrushMesh mesh, const AssetRef& material)
    {
        for (BrushFace& face : mesh.Faces)
            face.Material.Material = material;
        return mesh;
    }

    BrushMesh Shifted(BrushMesh mesh, Vec3d by)
    {
        for (BrushVertex& vertex : mesh.Vertices)
            vertex.Position = vertex.Position + by;
        return mesh;
    }
}

TEST(ShellGeneration, RoomsAWallApartShareOneWallKeptByItsOwner)
{
    // West room x in [-4, 0], east room x in [0.25, 4.25]: a wall's thickness apart.
    const BrushMesh west = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { -2, 0, 0 });
    const BrushMesh east = Painted(Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { 2.25f, 0, 0 }), kOther);

    const ShellNeighbour eastNeighbour{ &east, 0.25f, true };
    const ShellResult westShell = GenerateShell(west, Rules(0.25f), std::span(&eastNeighbour, 1));
    ASSERT_EQ(westShell.Slabs.size(), 6u);
    const BrushMesh& kept = SlabOn(westShell, west, { 1, 0, 0 }).Mesh;
    const bool eastSidePainted = std::any_of(kept.Faces.begin(), kept.Faces.end(), [](const BrushFace& face) {
        return face.Normal.X > 0.99f && SameAsset(face.Material.Material, kOther);
    });
    EXPECT_TRUE(eastSidePainted) << "the east room's side of the shared wall keeps the east room's material";

    const ShellNeighbour westNeighbour{ &west, 0.25f, false };
    const ShellResult eastShell = GenerateShell(east, Rules(0.25f), std::span(&westNeighbour, 1));
    EXPECT_EQ(eastShell.Slabs.size(), 5u) << "the east room's west wall is the shared one";
}

TEST(ShellGeneration, APartlySharedWallIsCutWhereTheNeighbourKeepsIt)
{
    // A narrower east room: its west face covers z in [-1, 1] of the west room's east face.
    const BrushMesh west = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { -2, 0, 0 });
    const BrushMesh east = Shifted(BrushOps::MakeBox({ 2, 1.5f, 1 }), { 2.25f, 0, 0 });
    const ShellNeighbour eastOwns{ &east, 0.25f, false };
    const ShellResult westShell = GenerateShell(west, Rules(0.25f), std::span(&eastOwns, 1));
    ASSERT_FALSE(westShell.Refused);
    for (const ShellProblem& problem : westShell.Problems)
        ADD_FAILURE() << problem.Message;

    float inner = 0.0f;
    for (const ShellSlab& slab : westShell.Slabs)
        if (west.Faces[slab.SourceFace].Normal.X > 0.99f)
            inner += FacingArea(slab.Mesh, { -1, 0, 0 });
    EXPECT_NEAR(inner, 6.0f * 3.0f - 2.0f * 3.0f, 1e-3f) << "the shared part is cut out of the west room's wall";
}

TEST(ShellGeneration, WallsOfDifferentThicknessAreNotShared)
{
    const BrushMesh west = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { -2, 0, 0 });
    const BrushMesh east = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { 2.25f, 0, 0 });
    const ShellNeighbour thicker{ &east, 0.5f, false };
    EXPECT_EQ(GenerateShell(west, Rules(0.25f), std::span(&thicker, 1)).Slabs.size(), 6u);
}

namespace
{
    // A door or window through the east wall of a room whose floor is y = 0.
    ShellOpening EastOpening(float x, float sill, float z, float width, float height)
    {
        ShellOpening opening;
        opening.Origin = { x, sill, z };
        opening.Across = { 0, 0, -1 };
        opening.Up = { 0, 1, 0 };
        opening.Through = { 1, 0, 0 };
        opening.Width = width;
        opening.Height = height;
        return opening;
    }

    float EastWallInside(const ShellResult& shell, const BrushMesh& volume)
    {
        float area = 0.0f;
        for (const ShellSlab& slab : shell.Slabs)
            if (volume.Faces[slab.SourceFace].Normal.X > 0.99f)
                area += FacingArea(slab.Mesh, { -1, 0, 0 });
        return area;
    }

    void ExpectNoProblems(const ShellResult& shell)
    {
        for (const ShellProblem& problem : shell.Problems)
            ADD_FAILURE() << problem.Message;
    }
}

TEST(ShellOpenings, ADoorAtFloorLevelNotchesTheWall)
{
    const BrushMesh room = Shifted(BrushOps::MakeBox({ 4, 1.5f, 3 }), { 0, 1.5f, 0 });
    ShellResult shell = GenerateShell(room, Rules(0.25f));
    const ShellOpening door = EastOpening(4.125f, 0.0f, 0.0f, 1.2f, 2.2f);
    CutOpenings(shell, 0.25f, std::span(&door, 1));
    ExpectNoProblems(shell);
    EXPECT_NEAR(EastWallInside(shell, room), 6.0f * 3.0f - 1.2f * 2.2f, 1e-3f);
    for (const ShellSlab& slab : shell.Slabs)
        EXPECT_TRUE(Closed(slab.Mesh));
}

TEST(ShellOpenings, AWindowCutsAHole)
{
    const BrushMesh room = Shifted(BrushOps::MakeBox({ 4, 1.5f, 3 }), { 0, 1.5f, 0 });
    ShellResult shell = GenerateShell(room, Rules(0.25f));
    const ShellOpening window = EastOpening(4.125f, 1.0f, 1.0f, 1.0f, 1.0f);
    CutOpenings(shell, 0.25f, std::span(&window, 1));
    ExpectNoProblems(shell);
    EXPECT_NEAR(EastWallInside(shell, room), 18.0f - 1.0f, 1e-3f);
}

TEST(ShellOpenings, AnOpeningOutOfReachCutsNothing)
{
    const BrushMesh room = Shifted(BrushOps::MakeBox({ 4, 1.5f, 3 }), { 0, 1.5f, 0 });
    ShellResult shell = GenerateShell(room, Rules(0.25f));
    const ShellOpening far = EastOpening(7.0f, 0.0f, 0.0f, 1.2f, 2.2f);
    CutOpenings(shell, 0.25f, std::span(&far, 1));
    EXPECT_NEAR(EastWallInside(shell, room), 18.0f, 1e-3f);
}

TEST(ShellOpenings, ADoorThroughASharedWallIsCutOnce)
{
    const BrushMesh west = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { -2, 1.5f, 0 });
    const BrushMesh east = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { 2.25f, 1.5f, 0 });
    const ShellNeighbour eastNeighbour{ &east, 0.25f, true };
    const ShellNeighbour westNeighbour{ &west, 0.25f, false };
    ShellResult westShell = GenerateShell(west, Rules(0.25f), std::span(&eastNeighbour, 1));
    ShellResult eastShell = GenerateShell(east, Rules(0.25f), std::span(&westNeighbour, 1));
    const ShellOpening door = EastOpening(0.125f, 0.0f, 0.0f, 1.2f, 2.2f);
    CutOpenings(westShell, 0.25f, std::span(&door, 1));
    CutOpenings(eastShell, 0.25f, std::span(&door, 1));
    ExpectNoProblems(westShell);
    EXPECT_NEAR(EastWallInside(westShell, west), 6.0f * 3.0f - 1.2f * 2.2f, 1e-3f);
    EXPECT_EQ(eastShell.Slabs.size(), 5u) << "the east room has no west wall of its own to cut";
}

TEST(ShellOpenings, AnOpeningOverAnEarlierOneIsRefused)
{
    const BrushMesh room = Shifted(BrushOps::MakeBox({ 4, 1.5f, 3 }), { 0, 1.5f, 0 });
    ShellResult shell = GenerateShell(room, Rules(0.25f));
    const ShellOpening openings[] = { EastOpening(4.125f, 1.0f, 0.0f, 1.0f, 1.0f),
                                      EastOpening(4.125f, 1.2f, 0.3f, 1.0f, 1.0f) };
    CutOpenings(shell, 0.25f, openings);
    ASSERT_EQ(shell.Problems.size(), 1u);
    EXPECT_EQ(shell.Problems.front().Kind, ShellProblemKind::OpeningRefused);
    EXPECT_EQ(shell.Problems.front().Element, 1u);
}

TEST(ShellGeneration, OverlappingSpacesAreRefusedButOneInAnLsNotchIsNot)
{
    const BrushMesh room = BrushOps::MakeBox({ 2, 1.5f, 2 });
    const BrushMesh crossing = Shifted(BrushOps::MakeBox({ 2, 1.5f, 2 }), { 1, 0, 0 });
    const ShellNeighbour overlap{ &crossing, 0.25f, false };
    const ShellResult refused = GenerateShell(room, Rules(0.25f), std::span(&overlap, 1));
    EXPECT_TRUE(refused.Refused);
    EXPECT_EQ(refused.Problems.front().Kind, ShellProblemKind::SpaceOverlap);

    const BrushMesh ell = Prism({ { 0, 0 }, { 0, 6 }, { 3, 6 }, { 3, 3 }, { 8, 3 }, { 8, 0 } }, 0, 3);
    const BrushMesh notch = Prism({ { 3.25f, 3.25f }, { 3.25f, 6 }, { 8, 6 }, { 8, 3.25f } }, 0, 3);
    const ShellNeighbour inTheNotch{ &notch, 0.25f, false };
    EXPECT_FALSE(GenerateShell(ell, Rules(0.25f), std::span(&inTheNotch, 1)).Refused);
}

TEST(ShellGeneration, WallsNearerThanTheirThicknessAreNoted)
{
    const BrushMesh west = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { -2, 0, 0 });
    const BrushMesh east = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { 2.1f, 0, 0 });
    const ShellNeighbour close{ &east, 0.25f, true };
    const ShellResult shell = GenerateShell(west, Rules(0.25f), std::span(&close, 1));
    EXPECT_FALSE(shell.Refused);
    EXPECT_EQ(shell.Slabs.size(), 6u);
    ASSERT_FALSE(shell.Problems.empty());
    EXPECT_EQ(shell.Problems.front().Kind, ShellProblemKind::WallsOverlap);
}

namespace
{
    std::size_t Count(const std::vector<ShellSurface>& surfaces, ShellSide side)
    {
        return static_cast<std::size_t>(std::ranges::count(surfaces, side, &ShellSurface::Side));
    }

    // Faces of `mesh` running across `through`: an opening's reveals.
    std::size_t Across(const BrushMesh& mesh, Vec3d through)
    {
        return static_cast<std::size_t>(std::ranges::count_if(mesh.Faces, [&](const BrushFace& face) {
            return std::abs(BrushComputeFaceNormal(mesh, face).Dot(through)) < 0.01f;
        }));
    }
}

TEST(ShellSurfaces, EachWallIsARoomSideAndASeparateOutside)
{
    const BrushMesh room = BrushOps::MakeBox({ 4, 1.5f, 3 });
    const std::vector<ShellSurface> surfaces = ShellSurfaces(GenerateShell(room, Rules(0.25f)));
    EXPECT_EQ(Count(surfaces, ShellSide::Room), 6u);
    EXPECT_EQ(Count(surfaces, ShellSide::Outside), 6u);
    EXPECT_EQ(Count(surfaces, ShellSide::NeighbourRoom), 0u);
    for (const ShellSurface& surface : surfaces)
    {
        if (surface.Side == ShellSide::Room)
        {
            EXPECT_EQ(surface.Mesh.Faces.size(), 1u) << "a plain wall's room side is the one face looking in";
        }
    }
}

TEST(ShellSurfaces, ASharedWallGivesTheNeighbourItsOwnSide)
{
    const BrushMesh west = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { -2, 0, 0 });
    const BrushMesh east = Shifted(BrushOps::MakeBox({ 2, 1.5f, 3 }), { 2.25f, 0, 0 });
    const ShellNeighbour eastNeighbour{ &east, 0.25f, true };
    const ShellResult shell = GenerateShell(west, Rules(0.25f), std::span(&eastNeighbour, 1));
    const std::vector<ShellSurface> surfaces = ShellSurfaces(shell);
    ASSERT_EQ(Count(surfaces, ShellSide::NeighbourRoom), 1u);
    const ShellSurface& theirs = *std::ranges::find(surfaces, ShellSide::NeighbourRoom, &ShellSurface::Side);
    EXPECT_EQ(shell.Slabs[theirs.Slab].SharedWith, std::optional<std::uint32_t>(0u));
    EXPECT_GT(BrushComputeFaceNormal(theirs.Mesh, theirs.Mesh.Faces.front()).X, 0.99f) << "it looks into the east room";
}

TEST(ShellSurfaces, AnOpeningsRevealsBelongToTheRoomSide)
{
    const BrushMesh room = Shifted(BrushOps::MakeBox({ 4, 1.5f, 3 }), { 0, 1.5f, 0 });
    ShellResult shell = GenerateShell(room, Rules(0.25f));
    const ShellOpening door = EastOpening(4.125f, 0.0f, 0.0f, 1.2f, 2.2f);
    CutOpenings(shell, 0.25f, std::span(&door, 1));
    for (const ShellSurface& surface : ShellSurfaces(shell))
    {
        if (room.Faces[shell.Slabs[surface.Slab].SourceFace].Normal.X < 0.99f)
            continue;
        const std::size_t across = Across(surface.Mesh, { 1, 0, 0 });
        if (surface.Side == ShellSide::Room)
        {
            EXPECT_EQ(across, 3u) << "the jambs and the head";
        }
        else
        {
            EXPECT_EQ(across, 3u) << "the outside keeps the wall's ends square to it: its top and its foot "
                                     "either side of the door";
        }
    }
}
