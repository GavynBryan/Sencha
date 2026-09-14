// Picking against a brush's evaluated pieces: generated geometry selects the
// entity and, in face mode, the source face it stands for; edges and vertices
// are grabbed on the source only.

#include "WorkspaceFixture.h"

#include "brush/BrushEvaluation.h"
#include "viewport/EditorViewport.h"
#include "viewport/Picking.h"
#include "viewport/ViewportProjection.h"

#include <algorithm>

namespace
{
class BrushPiecePickingTest : public WorkspaceTest
{
protected:
    [[nodiscard]] static EditorViewport TopViewport()
    {
        EditorViewport viewport;
        viewport.ApplyOrientation(ViewportOrientation::Top);
        viewport.Id = ViewportId{ 1 };
        viewport.RegionMin = ImVec2(0.0f, 0.0f);
        viewport.RegionMax = ImVec2(400.0f, 400.0f);
        return viewport;
    }

    [[nodiscard]] static ImVec2 PixelOf(const EditorViewport& viewport, Vec3d world)
    {
        const std::optional<ProjectedPoint> p = ViewportProjection(viewport).WorldToPixel(world);
        EXPECT_TRUE(p.has_value());
        return p.has_value() ? p->Pixel : ImVec2{};
    }

    // A unit box at the origin mirrored across x = 3: the copy spans x in [4, 6].
    [[nodiscard]] EntityId MirroredBrush()
    {
        const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
        BrushModifier mirror;
        MirrorModifier params; // origin-derived plane, 3 along X: x = 3
        params.Offset = 3.0f;
        mirror.Params = params;
        Scene().SetBrushModifiers(brush, { mirror });
        Scene().RefreshDerivedTransforms();
        return brush;
    }

    [[nodiscard]] std::uint32_t TopFace(EntityId brush)
    {
        const BrushMesh* mesh = Scene().TryGetBrushMesh(brush);
        for (std::uint32_t i = 0; i < mesh->Faces.size(); ++i)
            if (mesh->Faces[i].Normal.Y > 0.9f)
                return i;
        ADD_FAILURE() << "no top face";
        return 0;
    }
};
}

TEST_F(BrushPiecePickingTest, AGeneratedPieceSelectsTheEntity)
{
    const EntityId brush = MirroredBrush();
    const EditorViewport top = TopViewport();
    const SelectableRef hit = Workspace.Picking.Pick(top, PixelOf(top, { 5, 0, 0 }), Scene());
    EXPECT_TRUE(hit.IsEntity());
    EXPECT_EQ(hit.Entity, brush);
    // Nothing sits at x = 2.5 (between the source and its copy).
    EXPECT_FALSE(Workspace.Picking.Pick(top, PixelOf(top, { 2.5f, 0, 0 }), Scene()).IsValid());
}

TEST_F(BrushPiecePickingTest, AFaceOnACopySelectsTheSourceFace)
{
    const EntityId brush = MirroredBrush();
    const EditorViewport top = TopViewport();
    const SelectableRef hit = Workspace.Picking.Pick(
        top, PixelOf(top, { 5, 0, 0 }), Scene(), BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    ASSERT_TRUE(hit.IsFace());
    EXPECT_EQ(hit.Entity, brush);
    EXPECT_EQ(hit.ElementId, TopFace(brush));
}

TEST_F(BrushPiecePickingTest, VerticesAreGrabbedOnTheSourceOnly)
{
    const EntityId brush = MirroredBrush();
    const EditorViewport top = TopViewport();
    EXPECT_FALSE(Workspace.Picking.Pick(top, PixelOf(top, { 4, 1, -1 }), Scene(),
                                        BrushPickRequest{ .Mode = BrushPickMode::VertexOnly }).IsValid());
    const SelectableRef source = Workspace.Picking.Pick(
        top, PixelOf(top, { -1, 1, -1 }), Scene(), BrushPickRequest{ .Mode = BrushPickMode::VertexOnly });
    EXPECT_TRUE(source.IsVertex());
    EXPECT_EQ(source.Entity, brush);
}

TEST_F(BrushPiecePickingTest, SurfacePicksHitGeneratedPieces)
{
    (void)MirroredBrush();
    const EditorViewport top = TopViewport();
    const std::optional<SurfaceHit> hit = Workspace.Picking.PickSurface(top, PixelOf(top, { 5, 0, 0 }), Scene());
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Point.X, 5.0f, 1e-3f);
    EXPECT_NEAR(hit->Point.Y, 1.0f, 1e-3f);
    EXPECT_GT(hit->Normal.Y, 0.9f);
}

TEST_F(BrushPiecePickingTest, MarqueeOverEveryPieceYieldsEachSourceFaceOnce)
{
    const EntityId brush = MirroredBrush();
    const EditorViewport top = TopViewport();
    const ImVec2 a = PixelOf(top, { -2, 0, -2 });
    const ImVec2 b = PixelOf(top, { 7, 0, 2 });
    const std::vector<SelectableRef> faces = Workspace.Picking.PickInRect(
        top, ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)),
        Scene(), MeshElementKind::Face);
    // Both pieces project the top and bottom face centers into the rectangle;
    // each source face is reported once.
    std::vector<std::uint32_t> ids;
    for (const SelectableRef& ref : faces)
    {
        EXPECT_EQ(ref.Entity, brush);
        ids.push_back(ref.ElementId);
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
    EXPECT_FALSE(ids.empty());

    const std::vector<SelectableRef> objects = Workspace.Picking.PickInRect(
        top, ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)), ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)),
        Scene(), MeshElementKind::Object);
    EXPECT_EQ(objects.size(), 1u);
}

TEST(IntersectRayAabb, SlabTest)
{
    const Aabb3d box = Aabb3d::FromMinMax({ -1, -1, -1 }, { 1, 1, 1 });
    float near = -1.0f;
    EXPECT_TRUE(IntersectRayAabb(Ray3d({ -5, 0, 0 }, { 1, 0, 0 }), box, near));
    EXPECT_NEAR(near, 4.0f, 1e-5f);
    EXPECT_FALSE(IntersectRayAabb(Ray3d({ -5, 2, 0 }, { 1, 0, 0 }), box, near)); // parallel slab miss
    EXPECT_FALSE(IntersectRayAabb(Ray3d({ 5, 0, 0 }, { 1, 0, 0 }), box, near));  // box behind the ray
    EXPECT_TRUE(IntersectRayAabb(Ray3d({ 0, 0, 0 }, { 0, 0, 1 }), box, near));   // origin inside
    EXPECT_NEAR(near, 0.0f, 1e-6f);
    const Vec3d diagonal = Vec3d{ 1, 1, 1 }.Normalized();
    EXPECT_TRUE(IntersectRayAabb(Ray3d({ -3, -3, -3 }, diagonal), box, near));
    EXPECT_FALSE(IntersectRayAabb(Ray3d({ -3, -3, 3 }, diagonal), box, near));
    EXPECT_FALSE(IntersectRayAabb(Ray3d({ 0, 0, 0 }, { 1, 0, 0 }), Aabb3d::Empty(), near));
}

TEST_F(BrushPiecePickingTest, ALargeArrayPicksOnlyWhereACopyIs)
{
    // 10 x 10 unit boxes, 4 apart on X and Z: copy (i, k) spans [4i-1, 4i+1] x [4k-1, 4k+1].
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    BrushModifier alongX;
    ArrayModifier x;
    x.Axis = LocalAxis::X;
    x.Count = 10;
    x.Spacing = 2.0f;
    alongX.Params = x;
    BrushModifier alongZ;
    ArrayModifier z;
    z.Axis = LocalAxis::Z;
    z.Count = 10;
    z.Spacing = 2.0f;
    alongZ.Params = z;
    Scene().SetBrushModifiers(brush, { alongX, alongZ });
    Scene().RefreshDerivedTransforms();
    ASSERT_EQ(Scene().TryGetBrushPieces(brush)->Pieces.size(), 100u);

    const EditorViewport top = TopViewport();
    const SelectableRef body = Workspace.Picking.Pick(top, PixelOf(top, { 28, 0, 12 }), Scene());
    EXPECT_TRUE(body.IsEntity());
    EXPECT_EQ(body.Entity, brush);
    const SelectableRef face = Workspace.Picking.Pick(
        top, PixelOf(top, { 28, 0, 12 }), Scene(), BrushPickRequest{ .Mode = BrushPickMode::FaceOnly });
    ASSERT_TRUE(face.IsFace());
    EXPECT_EQ(face.ElementId, TopFace(brush));
    // The gap between copies (7, 3) and (8, 3).
    EXPECT_FALSE(Workspace.Picking.Pick(top, PixelOf(top, { 30, 0, 12 }), Scene()).IsValid());
    EXPECT_FALSE(Workspace.Picking.PickSurface(top, PixelOf(top, { 30, 0, 12 }), Scene()).has_value());
    const std::optional<SurfaceHit> surface =
        Workspace.Picking.PickSurface(top, PixelOf(top, { 28, 0, 12 }), Scene());
    ASSERT_TRUE(surface.has_value());
    EXPECT_NEAR(surface->Point.Y, 1.0f, 1e-3f);
}
