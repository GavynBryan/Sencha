#include "brush/BrushMeshSerialization.h"
#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"
#include "brush/FaceMaterial.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    UvProjection DefaultUv()
    {
        return UvProjection{}; // AxisU=(1,0,0), AxisV=(0,0,1), Scale=1, Offset=0, Rot=0
    }
}

//=============================================================================
// ProjectUv — the projection that makes "UVs survive resize" automatic.
//=============================================================================

TEST(ProjectUv, IdentityProjectionReadsPositionDirectly)
{
    const Vec2d uv = ProjectUv(DefaultUv(), Vec3d{ 2.0f, 5.0f, 3.0f });
    EXPECT_FLOAT_EQ(uv.X, 2.0f); // dot with AxisU=(1,0,0)
    EXPECT_FLOAT_EQ(uv.Y, 3.0f); // dot with AxisV=(0,0,1)
}

TEST(ProjectUv, ScaleIsWorldUnitsPerTile)
{
    UvProjection p = DefaultUv();
    p.Scale = { 2.0f, 4.0f };
    const Vec2d uv = ProjectUv(p, Vec3d{ 2.0f, 0.0f, 8.0f });
    EXPECT_FLOAT_EQ(uv.X, 1.0f); // 2 / 2
    EXPECT_FLOAT_EQ(uv.Y, 2.0f); // 8 / 4
}

TEST(ProjectUv, OffsetShiftsUv)
{
    UvProjection p = DefaultUv();
    p.Offset = { 0.5f, -0.25f };
    const Vec2d uv = ProjectUv(p, Vec3d{ 2.0f, 0.0f, 3.0f });
    EXPECT_FLOAT_EQ(uv.X, 2.5f);
    EXPECT_FLOAT_EQ(uv.Y, 2.75f);
}

TEST(ProjectUv, RotationSpinsTheUvBasis)
{
    UvProjection p = DefaultUv();
    p.Rotation = 90.0f; // U'=V=(0,0,1), V'=-U=(-1,0,0)
    const Vec2d uv = ProjectUv(p, Vec3d{ 2.0f, 0.0f, 3.0f });
    EXPECT_NEAR(uv.X, 3.0f, 1e-5f);  // dot(pos, (0,0,1))
    EXPECT_NEAR(uv.Y, -2.0f, 1e-5f); // dot(pos, (-1,0,0))
}

TEST(ProjectUv, ZeroScaleDoesNotDivideByZero)
{
    UvProjection p = DefaultUv();
    p.Scale = { 0.0f, 0.0f };
    const Vec2d uv = ProjectUv(p, Vec3d{ 2.0f, 0.0f, 3.0f });
    EXPECT_TRUE(std::isfinite(uv.X));
    EXPECT_TRUE(std::isfinite(uv.Y));
}

// The headline requirement: moving a vertex re-projects with CONSTANT density
// (no stretch). The UV delta equals the position delta along the axis / scale,
// independent of where the vertex started — this is texture lock.
TEST(ProjectUv, ResizeKeepsConstantDensity)
{
    UvProjection p = DefaultUv();
    p.Scale = { 2.0f, 2.0f };

    const Vec2d before = ProjectUv(p, Vec3d{ 1.0f, 0.0f, 1.0f });
    const Vec2d after = ProjectUv(p, Vec3d{ 5.0f, 0.0f, 1.0f }); // vertex slid +4 along U

    EXPECT_FLOAT_EQ(after.X - before.X, 4.0f / 2.0f); // density constant: ΔU = Δx / scale
    EXPECT_FLOAT_EQ(after.Y, before.Y);               // orthogonal axis unaffected
}

//=============================================================================
// World/face-aligned axis derivation.
//=============================================================================

TEST(UvProjectionForNormal, WorldAlignedPicksBoxPlaneByDominantAxis)
{
    const UvProjection up = UvProjectionForNormal(Vec3d{ 0.0f, 0.0f, 1.0f }, true);
    EXPECT_FLOAT_EQ(up.AxisU.X, 1.0f);
    EXPECT_FLOAT_EQ(up.AxisV.Y, 1.0f);

    const UvProjection side = UvProjectionForNormal(Vec3d{ 1.0f, 0.0f, 0.0f }, true);
    EXPECT_FLOAT_EQ(side.AxisU.Y, 1.0f);
    EXPECT_FLOAT_EQ(side.AxisV.Z, 1.0f);
}

TEST(UvProjectionForNormal, FaceAlignedBasisIsOrthonormalAndInPlane)
{
    const Vec3d n = Vec3d{ 1.0f, 2.0f, 3.0f }.Normalized();
    const UvProjection up = UvProjectionForNormal(n, false);
    EXPECT_NEAR(up.AxisU.Dot(n), 0.0f, 1e-5f);
    EXPECT_NEAR(up.AxisV.Dot(n), 0.0f, 1e-5f);
    EXPECT_NEAR(up.AxisU.Dot(up.AxisV), 0.0f, 1e-5f);
    EXPECT_NEAR(up.AxisU.Magnitude(), 1.0f, 1e-5f);
    EXPECT_NEAR(up.AxisV.Magnitude(), 1.0f, 1e-5f);
}

//=============================================================================
// UV justify presets (pure, over the points being justified).
//=============================================================================

TEST(UvProjectionJustify, FitMakesPointsSpanExactlyOneTile)
{
    // A 4x6 axis-aligned quad in the XZ plane, identity projection.
    const std::vector<Vec3d> quad = {
        { 0.0f, 0.0f, 0.0f }, { 4.0f, 0.0f, 0.0f },
        { 4.0f, 0.0f, 6.0f }, { 0.0f, 0.0f, 6.0f },
    };
    const UvProjection fitted = UvProjectionFit(DefaultUv(), quad);

    // Every corner now lands within [0,1], with the extremes at exactly 0 and 1.
    Vec2d lo{ 1.0f, 1.0f };
    Vec2d hi{ 0.0f, 0.0f };
    for (Vec3d p : quad)
    {
        const Vec2d uv = ProjectUv(fitted, p);
        lo.X = std::min(lo.X, uv.X); lo.Y = std::min(lo.Y, uv.Y);
        hi.X = std::max(hi.X, uv.X); hi.Y = std::max(hi.Y, uv.Y);
    }
    EXPECT_NEAR(lo.X, 0.0f, 1e-5f);
    EXPECT_NEAR(lo.Y, 0.0f, 1e-5f);
    EXPECT_NEAR(hi.X, 1.0f, 1e-5f);
    EXPECT_NEAR(hi.Y, 1.0f, 1e-5f);
}

TEST(UvProjectionJustify, CenterMapsBoundsCenterToHalf)
{
    const std::vector<Vec3d> quad = {
        { 2.0f, 0.0f, 0.0f }, { 6.0f, 0.0f, 0.0f },
        { 6.0f, 0.0f, 8.0f }, { 2.0f, 0.0f, 8.0f },
    };
    UvProjection p = DefaultUv();
    p.Scale = { 4.0f, 4.0f };
    const UvProjection centered = UvProjectionCenter(p, quad);

    // Scale is preserved; the bounds center (4, 4 in world) maps to (0.5, 0.5).
    EXPECT_FLOAT_EQ(centered.Scale.X, 4.0f);
    EXPECT_FLOAT_EQ(centered.Scale.Y, 4.0f);
    const Vec2d mid = ProjectUv(centered, Vec3d{ 4.0f, 0.0f, 4.0f });
    EXPECT_NEAR(mid.X, 0.5f, 1e-5f);
    EXPECT_NEAR(mid.Y, 0.5f, 1e-5f);
}

TEST(UvProjectionJustify, EmptyPointsLeaveProjectionUnchanged)
{
    const UvProjection p = DefaultUv();
    const UvProjection out = UvProjectionFit(p, {});
    EXPECT_FLOAT_EQ(out.Scale.X, p.Scale.X);
    EXPECT_FLOAT_EQ(out.Offset.X, p.Offset.X);
}

//=============================================================================
// EffectiveMaterial — the single "empty face ⇒ level default" resolver.
//=============================================================================

TEST(EffectiveMaterial, EmptyFaceResolvesToLevelDefault)
{
    const AssetRef levelDefault{ AssetType::Material, "asset://materials/dev/gray.smat" };
    FaceMaterial face; // Material left empty
    EXPECT_EQ(EffectiveMaterial(face, levelDefault).Path, levelDefault.Path);
}

TEST(EffectiveMaterial, SetFaceOverridesLevelDefault)
{
    const AssetRef levelDefault{ AssetType::Material, "asset://materials/dev/gray.smat" };
    FaceMaterial face;
    face.Material = AssetRef{ AssetType::Material, "asset://materials/dev/brick.smat" };
    EXPECT_EQ(EffectiveMaterial(face, levelDefault).Path, "asset://materials/dev/brick.smat");
}

//=============================================================================
// FaceMaterial survives the brush edit ops (it rides on BrushFace).
//=============================================================================

namespace
{
    int CountWithMaterial(const BrushMesh& mesh, std::string_view path)
    {
        int n = 0;
        for (const BrushFace& f : mesh.Faces)
            if (f.Material.Material.Path == path)
                ++n;
        return n;
    }
}

TEST(FaceMaterialSurvivesEdits, ExtrudeKeepsCapAndPropagatesToWalls)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    box.Faces[0].Material.Material = AssetRef{ AssetType::Material, "mat_a" };

    const BrushMesh out = BrushOps::ExtrudeFace(box, 0, 1.0f);
    // The cap (face 0) keeps mat_a; the four new walls inherit it: 5 faces total.
    EXPECT_GE(CountWithMaterial(out, "mat_a"), 5);
}

TEST(FaceMaterialSurvivesEdits, TranslatePreservesAllMaterials)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    box.Faces[2].Material.Material = AssetRef{ AssetType::Material, "mat_b" };

    const BrushMesh out = BrushOps::Translate(box, Vec3d{ 3.0f, 0.0f, 0.0f });
    EXPECT_EQ(CountWithMaterial(out, "mat_b"), 1);
}

TEST(FaceMaterialSurvivesEdits, DeleteFaceLeavesOtherMaterialsIntact)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    box.Faces[0].Material.Material = AssetRef{ AssetType::Material, "keep" };
    box.Faces[1].Material.Material = AssetRef{ AssetType::Material, "drop" };

    const BrushMesh out = BrushOps::DeleteFace(box, 1);
    EXPECT_EQ(CountWithMaterial(out, "keep"), 1);
}

TEST(FaceMaterialSurvivesEdits, ClipKeepsMaterialOnSurvivingPieces)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    for (BrushFace& f : box.Faces)
        f.Material.Material = AssetRef{ AssetType::Material, "wall" };

    // Cut in half through the origin; every surviving piece keeps "wall", and the
    // fresh cap carries the default (empty) material.
    const BrushMesh out = BrushOps::Clip(box, Plane{ Vec3d{ 1.0f, 0.0f, 0.0f }, 0.0f }, false);
    ASSERT_FALSE(out.Faces.empty());
    EXPECT_GE(CountWithMaterial(out, "wall"), 1);
    EXPECT_GE(CountWithMaterial(out, ""), 1); // the cap
}

//=============================================================================
// Serialization round-trips the projection + material, and tolerates the
// pre-texturing bare-array face form.
//=============================================================================

TEST(BrushMeshSerialization, RoundTripsFaceMaterialAndProjection)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    FaceMaterial& fm = box.Faces[0].Material;
    fm.Material = AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" };
    fm.Uv = UvProjectionForNormal(box.Faces[0].Normal, /*worldAligned*/ false);
    fm.Uv.Scale = { 2.0f, 3.0f };
    fm.Uv.Offset = { 0.25f, -0.5f };
    fm.Uv.Rotation = 45.0f;

    const std::string text = JsonStringify(BrushMeshToJson(box), /*pretty*/ false);
    const std::optional<JsonValue> parsed = JsonParse(text);
    ASSERT_TRUE(parsed.has_value());
    const BrushMesh back = BrushMeshFromJson(*parsed);

    ASSERT_EQ(back.Faces.size(), box.Faces.size());
    const FaceMaterial& got = back.Faces[0].Material;
    EXPECT_EQ(got.Material.Path, "asset://materials/dev/gray.smat");
    EXPECT_FLOAT_EQ(got.Uv.Scale.X, 2.0f);
    EXPECT_FLOAT_EQ(got.Uv.Scale.Y, 3.0f);
    EXPECT_FLOAT_EQ(got.Uv.Offset.X, 0.25f);
    EXPECT_FLOAT_EQ(got.Uv.Rotation, 45.0f);
    EXPECT_FALSE(got.Uv.WorldAligned);
    EXPECT_NEAR(got.Uv.AxisU.Dot(box.Faces[0].Normal), 0.0f, 1e-5f);

    // A face with no material round-trips empty (=> inherits level default).
    EXPECT_TRUE(back.Faces[1].Material.Material.Path.empty());
}

TEST(BrushMeshSerialization, LoadsPreTexturingBareArrayFaces)
{
    // The old sidecar form: faces are bare index arrays, no material/uv.
    const char* legacy = R"({
        "vertices": [[-1,-1,-1],[1,-1,-1],[1,1,-1],[-1,1,-1]],
        "faces": [[0,1,2,3]]
    })";
    const std::optional<JsonValue> parsed = JsonParse(legacy);
    ASSERT_TRUE(parsed.has_value());
    const BrushMesh mesh = BrushMeshFromJson(*parsed);

    ASSERT_EQ(mesh.Faces.size(), 1u);
    EXPECT_EQ(mesh.Faces[0].Loop.size(), 4u);
    EXPECT_TRUE(mesh.Faces[0].Material.Material.Path.empty());
}

//=============================================================================
// WorldUvProjection: the cross-brush bridge. The load-bearing property is the
// round trip: ToWorld then evaluating at the transformed point must equal the
// local projection at the local point, and ToLocal must invert ToWorld.
//=============================================================================

namespace
{
    Transform3f AwkwardTransform()
    {
        Transform3f t;
        t.Position = { 3.0f, -2.0f, 7.5f };
        t.Rotation = Quatf::FromAxisAngle(Vec3d{ 0.3f, 0.8f, -0.5f }.Normalized(), 1.1f);
        t.Scale = { 2.0f, 0.5f, 1.5f }; // nonuniform on purpose
        return t;
    }

    UvProjection AwkwardUv()
    {
        UvProjection p;
        p.AxisU = Vec3d{ 0.9f, 0.1f, 0.0f };
        p.AxisV = Vec3d{ 0.0f, 0.2f, 1.1f }; // deliberately not orthonormal
        p.Scale = { 2.0f, 0.25f };
        p.Offset = { 0.4f, -1.3f };
        p.Rotation = 33.0f;
        return p;
    }
}

TEST(WorldUvProjection, ToWorldMatchesLocalProjectionAtTransformedPoints)
{
    const Transform3f t = AwkwardTransform();
    const UvProjection local = AwkwardUv();
    const WorldUvProjection world = UvProjectionToWorld(local, t);

    const Vec3d samples[] = {
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 2.0f, 3.0f },
        { -4.5f, 0.25f, 9.0f },
    };
    for (Vec3d p : samples)
    {
        const Vec2d localUv = ProjectUv(local, p);
        const Vec2d worldUv = ProjectWorldUv(world, t.TransformPoint(p));
        EXPECT_NEAR(localUv.X, worldUv.X, 1e-3f);
        EXPECT_NEAR(localUv.Y, worldUv.Y, 1e-3f);
    }
}

TEST(WorldUvProjection, ToLocalInvertsToWorld)
{
    const Transform3f t = AwkwardTransform();
    const UvProjection original = AwkwardUv();
    const UvProjection roundTripped = UvProjectionToLocal(UvProjectionToWorld(original, t), t);

    // Rotation folds into the axes, so compare by evaluation, not fields.
    const Vec3d samples[] = {
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 2.0f, 3.0f },
        { -4.5f, 0.25f, 9.0f },
    };
    for (Vec3d p : samples)
    {
        const Vec2d a = ProjectUv(original, p);
        const Vec2d b = ProjectUv(roundTripped, p);
        EXPECT_NEAR(a.X, b.X, 1e-3f);
        EXPECT_NEAR(a.Y, b.Y, 1e-3f);
    }
    EXPECT_FLOAT_EQ(roundTripped.Rotation, 0.0f);
}

TEST(WorldUvProjection, SharedWorldMappingIsContinuousAcrossTwoBrushes)
{
    // Two abutting boxes with different transforms; one world projection baked
    // into each local frame must give identical UVs at the shared world point.
    Transform3f left;
    left.Position = { -1.0f, 0.0f, 0.0f };
    Transform3f right;
    right.Position = { 1.0f, 0.0f, 0.0f };
    right.Rotation = Quatf::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, 3.14159265f); // yawed 180
    right.Scale = { 2.0f, 1.0f, 1.0f };

    WorldUvProjection world;
    world.AxisU = { 1.0f, 0.0f, 0.0f };
    world.AxisV = { 0.0f, 0.0f, 1.0f };
    world.Scale = { 2.0f, 2.0f };
    world.Offset = { 0.25f, 0.0f };

    const UvProjection localLeft = UvProjectionToLocal(world, left);
    const UvProjection localRight = UvProjectionToLocal(world, right);

    const Vec3d seamWorld = { 0.0f, 0.0f, 0.5f };
    // The same world point expressed in each brush's local frame.
    const auto worldToLocal = [](const Transform3f& t, Vec3d w)
    {
        const Vec3d unrotated = t.Rotation.Inverse().RotateVector(w - t.Position);
        return Vec3d{ unrotated.X / t.Scale.X, unrotated.Y / t.Scale.Y, unrotated.Z / t.Scale.Z };
    };
    const Vec2d uvLeft = ProjectUv(localLeft, worldToLocal(left, seamWorld));
    const Vec2d uvRight = ProjectUv(localRight, worldToLocal(right, seamWorld));
    EXPECT_NEAR(uvLeft.X, uvRight.X, 1e-4f);
    EXPECT_NEAR(uvLeft.Y, uvRight.Y, 1e-4f);
    // And both match the world mapping directly.
    const Vec2d uvWorld = ProjectWorldUv(world, seamWorld);
    EXPECT_NEAR(uvLeft.X, uvWorld.X, 1e-4f);
    EXPECT_NEAR(uvLeft.Y, uvWorld.Y, 1e-4f);
}

TEST(WorldUvProjection, WorldFitSpansUnitTileAcrossUnionOfPoints)
{
    WorldUvProjection p;
    const Vec3d points[] = {
        { -3.0f, 0.0f, 1.0f }, // one brush's face
        { 1.0f, 0.0f, 2.0f },
        { 5.0f, 0.0f, 4.0f },  // another brush's face
    };
    const WorldUvProjection fit = WorldUvProjectionFit(p, points);
    const Vec2d atMin = ProjectWorldUv(fit, { -3.0f, 0.0f, 1.0f });
    const Vec2d atMax = ProjectWorldUv(fit, { 5.0f, 0.0f, 4.0f });
    EXPECT_NEAR(atMin.X, 0.0f, 1e-4f);
    EXPECT_NEAR(atMin.Y, 0.0f, 1e-4f);
    EXPECT_NEAR(atMax.X, 1.0f, 1e-4f);
    EXPECT_NEAR(atMax.Y, 1.0f, 1e-4f);
}

TEST(WorldUvProjection, DegenerateTransformScaleStaysTotal)
{
    Transform3f t;
    t.Scale = { 0.0f, 1.0f, 1.0f }; // collapsed axis
    const UvProjection local = DefaultUv();
    const WorldUvProjection world = UvProjectionToWorld(local, t);
    const UvProjection back = UvProjectionToLocal(world, t);
    // No NaNs/infs; evaluation stays finite.
    const Vec2d uv = ProjectUv(back, { 1.0f, 2.0f, 3.0f });
    EXPECT_TRUE(std::isfinite(uv.X));
    EXPECT_TRUE(std::isfinite(uv.Y));
}

TEST(FaceMaterialSurvivesEdits, ExtrudeWallsCarryOffsetAndRotation)
{
    BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    box.Faces[0].Material.Material = AssetRef{ AssetType::Material, "mat_a" };
    box.Faces[0].Material.Uv.Scale = { 2.0f, 2.0f };
    box.Faces[0].Material.Uv.Offset = { 0.25f, -0.5f };
    box.Faces[0].Material.Uv.Rotation = 15.0f;

    const BrushMesh out = BrushOps::ExtrudeFace(box, 0, 1.0f);
    // The walls continue the cap's texture: density AND phase carry over so the
    // texture does not reset at the shared edge.
    int walls = 0;
    for (const BrushFace& f : out.Faces)
    {
        if (f.Material.Material.Path != "mat_a")
            continue;
        EXPECT_FLOAT_EQ(f.Material.Uv.Scale.X, 2.0f);
        EXPECT_FLOAT_EQ(f.Material.Uv.Offset.X, 0.25f);
        EXPECT_FLOAT_EQ(f.Material.Uv.Offset.Y, -0.5f);
        EXPECT_FLOAT_EQ(f.Material.Uv.Rotation, 15.0f);
        ++walls;
    }
    EXPECT_GE(walls, 5); // cap + four walls
}

TEST(FaceMaterialSurvivesEdits, EdgeExtrudeInheritsSeedFaceMaterial)
{
    BrushMesh plane = BrushOps::MakePlane({ 1.0f, 0.0f, 1.0f }, 1);
    plane.Faces[0].Material.Material = AssetRef{ AssetType::Material, "floor" };
    plane.Faces[0].Material.Uv.Scale = { 4.0f, 4.0f };
    plane.Faces[0].Material.Uv.Offset = { 0.1f, 0.2f };

    const std::uint32_t a = plane.Faces[0].Loop[0];
    const std::uint32_t b = plane.Faces[0].Loop[1];
    const BrushMesh out = BrushOps::ExtrudeEdge(plane, a, b, { 0.0f, 1.0f, 0.0f },
                                                &plane.Faces[0].Material);

    const BrushFace& strip = out.Faces.back();
    EXPECT_EQ(strip.Material.Material.Path, "floor");
    EXPECT_FLOAT_EQ(strip.Material.Uv.Scale.X, 4.0f);
    EXPECT_FLOAT_EQ(strip.Material.Uv.Offset.X, 0.1f);
    EXPECT_FLOAT_EQ(strip.Material.Uv.Offset.Y, 0.2f);
}

TEST(FaceMaterialSurvivesEdits, EdgeExtrudeWithoutSeedKeepsDefaults)
{
    BrushMesh plane = BrushOps::MakePlane({ 1.0f, 0.0f, 1.0f }, 1);
    const std::uint32_t a = plane.Faces[0].Loop[0];
    const std::uint32_t b = plane.Faces[0].Loop[1];
    const BrushMesh out = BrushOps::ExtrudeEdge(plane, a, b, { 0.0f, 1.0f, 0.0f });
    EXPECT_TRUE(out.Faces.back().Material.Material.Path.empty());
}
