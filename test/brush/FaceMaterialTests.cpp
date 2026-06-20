#include "level/brush/BrushMeshSerialization.h"
#include "level/brush/BrushOps.h"
#include "level/brush/BrushValidation.h"
#include "level/brush/FaceMaterial.h"

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
