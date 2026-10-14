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
