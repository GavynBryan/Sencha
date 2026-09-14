#include "brush/BrushEvaluation.h"
#include "brush/BrushOps.h"
#include "brush/MirrorModifier.h"
#include "brush/BrushWorkCounters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace
{
    BrushModifier Mirror(Vec3d normal, float d = 0.0f)
    {
        BrushModifier m;
        MirrorModifier mirror;
        mirror.Source = MirrorPlaneSource::Custom;
        mirror.CustomPlane = Plane::FromNormalAndDistance(normal, d);
        m.Params = mirror;
        return m;
    }

    BrushModifier Array(int count, Vec3d offset)
    {
        BrushModifier m;
        ArrayModifier array;
        array.Placement = ArrayPlacement::ConstantOffset;
        array.Count = count;
        array.Offset = offset;
        m.Params = array;
        return m;
    }

    const BrushEvaluationPolicy kPolicy = BrushEvaluationPolicy::Cook();
}

TEST(BrushEvaluation, EmptyStackAliasesTheSourceWithoutCopying)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(box, {}, kPolicy);
    ASSERT_EQ(e.Pieces.size(), 1u);
    EXPECT_EQ(e.Pieces[0].Mesh, &box);
    EXPECT_TRUE(e.Generated.empty());
    EXPECT_EQ(e.Status, BrushEvaluationStatus::Ok);
    EXPECT_TRUE(e.Pieces[0].Placement.NearlyEquals(Transform3f::Identity(), 1e-6f));
}

TEST(BrushEvaluation, DisabledModifierIsSkipped)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    BrushModifier array = Array(5, { 2, 0, 0 });
    array.Enabled = false;
    const BrushEvaluated e = EvaluateBrushModifiers(box, { array }, kPolicy);
    EXPECT_EQ(e.Pieces.size(), 1u);
}

TEST(BrushEvaluation, ArrayIsPlacementsOfOneMesh)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(box, { Array(4, { 3, 0, 1 }) }, kPolicy);
    ASSERT_EQ(e.Pieces.size(), 4u);
    EXPECT_TRUE(e.Generated.empty());
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(e.Pieces[i].Mesh, &box);
        EXPECT_EQ(e.Pieces[i].Ordinal, i);
        EXPECT_NEAR(e.Pieces[i].Placement.Position.X, 3.0f * i, 1e-6f);
        EXPECT_NEAR(e.Pieces[i].Placement.Position.Z, 1.0f * i, 1e-6f);
    }
}

TEST(BrushEvaluation, MirrorMintsOneReflectedMeshSharedByPieces)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { Array(3, { 4, 0, 0 }), Mirror({ 1, 0, 0 }) }, kPolicy);
    ASSERT_EQ(e.Pieces.size(), 6u);
    ASSERT_EQ(e.Generated.size(), 1u);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_EQ(e.Pieces[i].Mesh, &box);
        EXPECT_EQ(e.Pieces[3 + i].Mesh, e.Generated[0].get());
        // A piece at +4i reflects to -4i.
        EXPECT_NEAR(e.Pieces[3 + i].Placement.Position.X, -4.0f * i, 1e-6f);
    }
}

TEST(BrushEvaluation, MirrorThenArrayDiffersFromArrayThenMirror)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated ma = EvaluateBrushModifiers(
        box, { Mirror({ 1, 0, 0 }, -2.0f), Array(3, { 10, 0, 0 }) }, kPolicy);
    const BrushEvaluated am = EvaluateBrushModifiers(
        box, { Array(3, { 10, 0, 0 }), Mirror({ 1, 0, 0 }, -2.0f) }, kPolicy);
    ASSERT_EQ(ma.Pieces.size(), 6u);
    ASSERT_EQ(am.Pieces.size(), 6u);
    // Mirror then Array: pairs march along +X. Array then Mirror: a row and its
    // reflection marching along -X.
    float maMaxX = -1e9f, amMinX = 1e9f;
    for (const BrushPiece& p : ma.Pieces) maMaxX = std::max(maMaxX, p.Placement.Position.X);
    for (const BrushPiece& p : am.Pieces) amMinX = std::min(amMinX, p.Placement.Position.X);
    EXPECT_NEAR(maMaxX, 20.0f, 1e-5f);
    EXPECT_NEAR(amMinX, -20.0f, 1e-5f);
}

TEST(BrushEvaluation, SourceSurvivesAsOrdinalZeroForMirrorAndArray)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    for (const BrushModifierStack& stack : { BrushModifierStack{ Mirror({ 0, 1, 0 }) },
                                             BrushModifierStack{ Array(3, { 1, 0, 0 }), Mirror({ 0, 0, 1 }) },
                                             BrushModifierStack{ Mirror({ 1, 0, 0 }), Array(2, { 0, 1, 0 }) } })
    {
        const BrushEvaluated e = EvaluateBrushModifiers(box, stack, kPolicy);
        EXPECT_EQ(e.Pieces[0].Mesh, &box);
        EXPECT_TRUE(e.Pieces[0].Placement.NearlyEquals(Transform3f::Identity(), 1e-6f));
    }
}

TEST(BrushEvaluation, SourceFaceForIsTheIdentityMapOnEveryPiece)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { Mirror({ 1, 0, 0 }), Array(2, { 5, 0, 0 }) }, kPolicy);
    for (const BrushPiece& piece : e.Pieces)
        for (std::uint32_t f = 0; f < box.Faces.size(); ++f)
            EXPECT_EQ(SourceFaceFor(e, piece, f), std::optional<std::uint32_t>{ f });
    EXPECT_FALSE(SourceFaceFor(e, e.Pieces[0], 99).has_value());
}

TEST(BrushEvaluation, IsDeterministic)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 2, 3 });
    const BrushModifierStack stack{ Mirror({ 0.7071f, 0, 0.7071f }, 1.0f), Array(3, { 1, 2, 3 }) };
    const BrushEvaluated a = EvaluateBrushModifiers(box, stack, kPolicy);
    const BrushEvaluated b = EvaluateBrushModifiers(box, stack, kPolicy);
    ASSERT_EQ(a.Pieces.size(), b.Pieces.size());
    for (std::size_t i = 0; i < a.Pieces.size(); ++i)
    {
        EXPECT_TRUE(a.Pieces[i].Placement.NearlyEquals(b.Pieces[i].Placement, 0.0f));
        ASSERT_EQ(a.Pieces[i].Mesh->Vertices.size(), b.Pieces[i].Mesh->Vertices.size());
        for (std::size_t v = 0; v < a.Pieces[i].Mesh->Vertices.size(); ++v)
            EXPECT_EQ(a.Pieces[i].Mesh->Vertices[v].Position, b.Pieces[i].Mesh->Vertices[v].Position);
    }
}

TEST(BrushEvaluation, RebaseKeepsTheEvaluatedGeometryInPlace)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    BrushModifierStack stack{ Mirror({ 1, 0, 0 }, -2.0f) };
    const BrushEvaluated before = EvaluateBrushModifiers(box, stack, kPolicy);

    const Vec3d delta{ 3.0f, -1.0f, 0.5f };
    const BrushMesh shifted = BrushOps::Translate(box, delta);
    RebaseBrushModifiers(stack, delta);
    const BrushEvaluated after = EvaluateBrushModifiers(shifted, stack, kPolicy);

    // In the new local frame everything is +delta; undoing that shift must
    // reproduce the old pieces exactly.
    ASSERT_EQ(after.Pieces.size(), before.Pieces.size());
    for (std::size_t i = 0; i < before.Pieces.size(); ++i)
    {
        const BrushMesh& a = *before.Pieces[i].Mesh;
        const BrushMesh& b = *after.Pieces[i].Mesh;
        for (std::size_t v = 0; v < a.Vertices.size(); ++v)
        {
            const Vec3d pa = before.Pieces[i].Placement.TransformPoint(a.Vertices[v].Position) + delta;
            const Vec3d pb = after.Pieces[i].Placement.TransformPoint(b.Vertices[v].Position);
            EXPECT_NEAR(pa.X, pb.X, 1e-5f);
            EXPECT_NEAR(pa.Y, pb.Y, 1e-5f);
            EXPECT_NEAR(pa.Z, pb.Z, 1e-5f);
        }
    }
}

TEST(BrushEvaluation, BudgetStopsBeforeTheOffendingModifier)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { Array(1000, { 1, 0, 0 }), Array(1000, { 0, 1, 0 }) }, BrushEvaluationPolicy::Interactive(4096));
    EXPECT_EQ(e.Status, BrushEvaluationStatus::PieceBudgetExceeded);
    EXPECT_EQ(e.FailedModifier, 1u);
    EXPECT_EQ(e.Pieces.size(), 1000u);
    EXPECT_TRUE(e.Generated.empty());
}

TEST(BrushEvaluation, TwentyMirrorsReportTheFirstOverflow)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    BrushModifierStack stack;
    for (int i = 0; i < 20; ++i)
        stack.push_back(Mirror({ 1, 0, 0 }, static_cast<float>(i)));
    const BrushEvaluated e = EvaluateBrushModifiers(box, stack, BrushEvaluationPolicy::Cook());
    EXPECT_EQ(e.Status, BrushEvaluationStatus::PieceBudgetExceeded);
    // 2^16 = 65536 is allowed; the 17th mirror would make 2^17.
    EXPECT_EQ(e.FailedModifier, 16u);
    EXPECT_EQ(e.Pieces.size(), 65536u);
}

TEST(BrushEvaluation, HugeCountCannotOverflowTheCheck)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { Array(2, { 1, 0, 0 }), Array(2147483647, { 0, 1, 0 }) }, BrushEvaluationPolicy::Cook());
    EXPECT_EQ(e.Status, BrushEvaluationStatus::PieceBudgetExceeded);
    EXPECT_EQ(e.FailedModifier, 1u);
    EXPECT_EQ(e.Pieces.size(), 2u);
}

TEST(BrushEvaluationPolicy, InteractiveNeverExceedsCook)
{
    EXPECT_EQ(BrushEvaluationPolicy::Interactive(1u << 30).MaxPieces, BrushEvaluationPolicy::Cook().MaxPieces);
    EXPECT_EQ(BrushEvaluationPolicy::Interactive(64).MaxPieces, 64u);
}

TEST(BrushEvaluation, PieceWorldTransformComposesTheEntityTransform)
{
    BrushPiece piece;
    piece.Placement.Position = { 1, 0, 0 };
    Transform3f entity = Transform3f::Identity();
    entity.Position = { 0, 5, 0 };
    entity.Scale = { 2, 2, 2 };
    const Transform3f world = PieceWorldTransform(entity, piece);
    EXPECT_NEAR(world.Position.X, 2.0f, 1e-6f);
    EXPECT_NEAR(world.Position.Y, 5.0f, 1e-6f);
}

//=============================================================================
// Relationship semantics: origin-derived mirrors and bounds-relative arrays.
//=============================================================================

namespace
{
    BrushModifier OriginMirror(LocalAxis axis, float offset = 0.0f)
    {
        BrushModifier m;
        MirrorModifier mirror;
        mirror.Axis = axis;
        mirror.Offset = offset;
        m.Params = mirror;
        return m;
    }

    BrushModifier BoundsArray(LocalAxis axis, int count, float gap, bool reverse = false)
    {
        BrushModifier m;
        ArrayModifier array;
        array.Axis = axis;
        array.Count = count;
        array.Spacing = gap;
        array.Reverse = reverse;
        m.Params = array;
        return m;
    }

    Aabb3d PieceLocalBounds(const BrushPiece& piece)
    {
        const Aabb3d local = BrushComputeBounds(*piece.Mesh);
        return Aabb3d{ local.Min + piece.Placement.Position, local.Max + piece.Placement.Position };
    }
}

TEST(ResolveMirrorPlane, OriginBoundsCenterAndCustom)
{
    const Aabb3d bounds{ Vec3d{ 2, 0, 0 }, Vec3d{ 6, 2, 2 } }; // center x = 4
    MirrorModifier m;
    m.Axis = LocalAxis::Z;
    m.Offset = 1.5f;
    const Plane origin = ResolveMirrorPlane(m, bounds);
    EXPECT_FLOAT_EQ(origin.Normal.Z, 1.0f);
    EXPECT_NEAR(origin.SignedDistanceTo({ 0, 0, 1.5f }), 0.0f, 1e-6f);

    m.Axis = LocalAxis::X;
    m.Source = MirrorPlaneSource::BoundsCenter;
    const Plane center = ResolveMirrorPlane(m, bounds);
    EXPECT_NEAR(center.SignedDistanceTo({ 5.5f, 0, 0 }), 0.0f, 1e-6f); // 4 + 1.5

    m.Source = MirrorPlaneSource::Custom;
    m.CustomPlane = Plane::FromNormalAndDistance({ 0, 1, 0 }, -9.0f);
    EXPECT_FLOAT_EQ(ResolveMirrorPlane(m, bounds).D, -9.0f);
}

TEST(ResolveArrayStep, ExtentPlusGapAlongTheAxisAndReverse)
{
    const Aabb3d bounds{ Vec3d{ -64, 0, -1 }, Vec3d{ 64, 3, 1 } };
    ArrayModifier a;
    a.Axis = LocalAxis::X;
    a.Spacing = 16.0f;
    EXPECT_FLOAT_EQ(ResolveArrayStep(a, bounds).X, 144.0f);
    a.Axis = LocalAxis::Y;
    EXPECT_FLOAT_EQ(ResolveArrayStep(a, bounds).Y, 19.0f);
    a.Reverse = true;
    EXPECT_FLOAT_EQ(ResolveArrayStep(a, bounds).Y, -19.0f);
    a.Placement = ArrayPlacement::ConstantOffset;
    a.Offset = { 1, 2, 3 };
    EXPECT_FLOAT_EQ(ResolveArrayStep(a, bounds).Z, -3.0f);
}

TEST(BrushEvaluation, BoundsArrayCopiesTouchAndFollowTheSourceWidth)
{
    const BrushMesh wall = BrushOps::MakeBox({ 64, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(wall, { BoundsArray(LocalAxis::X, 5, 0.0f) }, kPolicy);
    ASSERT_EQ(e.Pieces.size(), 5u);
    for (std::size_t i = 1; i < 5; ++i)
        EXPECT_NEAR(PieceLocalBounds(e.Pieces[i]).Min.X, PieceLocalBounds(e.Pieces[i - 1]).Max.X, 1e-4f);

    const BrushMesh wider = BrushOps::MakeBox({ 96, 1, 1 });
    const BrushEvaluated w = EvaluateBrushModifiers(wider, { BoundsArray(LocalAxis::X, 5, 0.0f) }, kPolicy);
    for (std::size_t i = 1; i < 5; ++i)
        EXPECT_NEAR(PieceLocalBounds(w.Pieces[i]).Min.X, PieceLocalBounds(w.Pieces[i - 1]).Max.X, 1e-4f);

    const BrushEvaluated g = EvaluateBrushModifiers(wall, { BoundsArray(LocalAxis::X, 5, 16.0f) }, kPolicy);
    for (std::size_t i = 1; i < 5; ++i)
        EXPECT_NEAR(PieceLocalBounds(g.Pieces[i]).Min.X - PieceLocalBounds(g.Pieces[i - 1]).Max.X, 16.0f, 1e-4f);
}

TEST(BrushEvaluation, ArrayAfterMirrorRepeatsTheMirroredPair)
{
    // Wall at x in [2, 4]; mirrored across the origin the pair spans [-4, 4].
    const BrushMesh wall = BrushOps::Translate(BrushOps::MakeBox({ 1, 1, 1 }), { 3, 0, 0 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        wall, { OriginMirror(LocalAxis::X), BoundsArray(LocalAxis::X, 3, 2.0f) }, kPolicy);
    ASSERT_EQ(e.Pieces.size(), 6u);
    ASSERT_EQ(e.Stages.size(), 2u);
    EXPECT_TRUE(e.Stages[1].Applied);
    EXPECT_NEAR(e.Stages[1].InputBounds.Min.X, -4.0f, 1e-5f);
    EXPECT_NEAR(e.Stages[1].InputBounds.Max.X, 4.0f, 1e-5f);
    EXPECT_NEAR(e.Stages[1].ArrayStep.X, 10.0f, 1e-5f); // pair width 8 + gap 2
    EXPECT_NEAR(e.Stages[0].MirrorPlane.SignedDistanceTo({ 0, 0, 0 }), 0.0f, 1e-6f);

    // The other order repeats the wall alone, then mirrors the whole row.
    const BrushEvaluated other = EvaluateBrushModifiers(
        wall, { BoundsArray(LocalAxis::X, 3, 2.0f), OriginMirror(LocalAxis::X) }, kPolicy);
    ASSERT_EQ(other.Pieces.size(), 6u);
    EXPECT_NEAR(other.Stages[0].ArrayStep.X, 4.0f, 1e-5f); // wall width 2 + gap 2
    EXPECT_NEAR(PieceSetLocalBounds(other.Meshes, other.Pieces).Max.X, 12.0f, 1e-5f);
    EXPECT_NEAR(PieceSetLocalBounds(e.Meshes, e.Pieces).Max.X, 24.0f, 1e-5f);
}

TEST(BrushEvaluation, StagesRecordDisabledAndStoppedEntries)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    BrushModifier off = OriginMirror(LocalAxis::Y);
    off.Enabled = false;
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { off, BoundsArray(LocalAxis::X, 100, 0.0f), BoundsArray(LocalAxis::Y, 100, 0.0f) },
        BrushEvaluationPolicy::Interactive(500));
    ASSERT_EQ(e.Stages.size(), 3u);
    EXPECT_FALSE(e.Stages[0].Applied);
    EXPECT_TRUE(e.Stages[1].Applied);
    EXPECT_FALSE(e.Stages[2].Applied);
    EXPECT_EQ(e.FailedModifier, 2u);
}

TEST(RebaseBrushModifiers, OnlyCustomPlanesMove)
{
    BrushModifier custom = OriginMirror(LocalAxis::X);
    std::get<MirrorModifier>(custom.Params).Source = MirrorPlaneSource::Custom;
    std::get<MirrorModifier>(custom.Params).CustomPlane = Plane::FromNormalAndDistance({ 1, 0, 0 }, -2.0f);
    BrushModifierStack stack{ OriginMirror(LocalAxis::X, 1.0f), custom, BoundsArray(LocalAxis::X, 2, 0.0f) };
    RebaseBrushModifiers(stack, { 5, 0, 0 });
    EXPECT_FLOAT_EQ(std::get<MirrorModifier>(stack[0].Params).Offset, 1.0f);
    EXPECT_FLOAT_EQ(std::get<MirrorModifier>(stack[1].Params).CustomPlane.D, -7.0f);
}

TEST(BrushEvaluation, MeshesListTheSourceFirstThenEachMintedMeshWithSignatures)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { Mirror({ 1, 0, 0 }, 4.0f), Array(3, { 0, 0, 2 }) }, kPolicy);
    ASSERT_EQ(e.Meshes.size(), 2u);
    EXPECT_EQ(e.Meshes[0].Mesh, &box);
    EXPECT_EQ(e.Meshes[1].Mesh, e.Generated[0].get());
    EXPECT_EQ(e.Meshes[0].Signature, BrushMeshSignature(box));
    EXPECT_NE(e.Meshes[0].Signature, e.Meshes[1].Signature);
    EXPECT_NEAR(e.Meshes[0].LocalBounds.Max.X, 1.0f, 1e-6f);
    for (const BrushPiece& piece : e.Pieces)
    {
        ASSERT_LT(piece.MeshIndex, e.Meshes.size());
        EXPECT_EQ(e.Meshes[piece.MeshIndex].Mesh, piece.Mesh);
    }
}

TEST(BrushEvaluation, SignatureFollowsContentNotIdentity)
{
    const BrushMesh a = BrushOps::MakeBox({ 1, 1, 1 });
    BrushMesh b = a;
    EXPECT_EQ(BrushMeshSignature(a), BrushMeshSignature(b));
    b.Vertices[0].Position.X += 0.5f;
    EXPECT_NE(BrushMeshSignature(a), BrushMeshSignature(b));
    BrushMesh c = a;
    c.Faces[0].Material.Material.Path = "asset://materials/other.smat";
    EXPECT_NE(BrushMeshSignature(a), BrushMeshSignature(c));
    BrushMesh d = a;
    BrushSetEdgeSoft(d, d.Faces[0].Loop[0], d.Faces[0].Loop[1], true);
    EXPECT_NE(BrushMeshSignature(a), BrushMeshSignature(d));
    BrushMesh f = a;
    std::rotate(f.Faces[0].Loop.begin(), f.Faces[0].Loop.begin() + 1, f.Faces[0].Loop.end());
    EXPECT_NE(BrushMeshSignature(a), BrushMeshSignature(f));
}

TEST(BrushEvaluation, ProjectedPieceCountMultipliesEnabledFactorsAndSaturates)
{
    EXPECT_EQ(BrushProjectedPieceCount({}), 1u);
    BrushModifier off = Mirror({ 1, 0, 0 });
    off.Enabled = false;
    EXPECT_EQ(BrushProjectedPieceCount({ off }), 1u);
    EXPECT_EQ(BrushProjectedPieceCount({ Mirror({ 1, 0, 0 }), Array(7, { 1, 0, 0 }) }), 14u);
    const BrushModifierStack huge = { Array(1 << 30, { 1, 0, 0 }), Array(1 << 30, { 1, 0, 0 }),
                                      Array(1 << 30, { 1, 0, 0 }) };
    EXPECT_EQ(BrushProjectedPieceCount(huge), std::numeric_limits<std::uint64_t>::max());
}

TEST(BrushEvaluation, PieceWorldBoundsAreVertexTightUnderRotationAndScale)
{
    const BrushMesh box = BrushOps::MakeBox({ 2, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { Mirror({ 1, 0, 0 }, 4.0f), Array(3, { 0, 0, 5 }) }, kPolicy);
    Transform3f world;
    world.Position = { 10, -3, 7 };
    world.Rotation = Quat<float>::FromAxisAngle({ 0, 1, 0 }, 0.7f)
        * Quat<float>::FromAxisAngle({ 1, 0, 0 }, 0.3f);
    world.Scale = { 2, 1, 0.5f };

    std::size_t visited = 0;
    ForEachPieceWorldBounds(e, world, [&](const BrushPiece& piece, const Aabb3d& bounds)
    {
        const Aabb3d tight = BrushWorldBounds(*piece.Mesh, PieceWorldTransform(world, piece));
        EXPECT_NEAR(bounds.Min.X, tight.Min.X, 1e-4f);
        EXPECT_NEAR(bounds.Min.Y, tight.Min.Y, 1e-4f);
        EXPECT_NEAR(bounds.Min.Z, tight.Min.Z, 1e-4f);
        EXPECT_NEAR(bounds.Max.X, tight.Max.X, 1e-4f);
        EXPECT_NEAR(bounds.Max.Y, tight.Max.Y, 1e-4f);
        EXPECT_NEAR(bounds.Max.Z, tight.Max.Z, 1e-4f);
        ++visited;
    });
    EXPECT_EQ(visited, e.Pieces.size());
}

TEST(BrushEvaluation, PieceWorldBoundsHandleARotatingPlacement)
{
    const BrushMesh box = BrushOps::MakeBox({ 2, 1, 1 });
    BrushEvaluated e = EvaluateBrushModifiers(box, {}, kPolicy);
    BrushPiece turned = e.Pieces[0];
    turned.Placement.Position = { 5, 0, 0 };
    turned.Placement.Rotation = Quat<float>::FromAxisAngle({ 0, 0, 1 }, 1.0f);
    turned.Ordinal = 1;
    e.Pieces.push_back(turned);
    Transform3f world;
    world.Position = { 1, 2, 3 };
    world.Rotation = Quat<float>::FromAxisAngle({ 0, 1, 0 }, 0.4f);

    EXPECT_FALSE(IsPureTranslation(turned.Placement));
    EXPECT_TRUE(IsPureTranslation(e.Pieces[0].Placement));
    ForEachPieceWorldBounds(e, world, [&](const BrushPiece& piece, const Aabb3d& bounds)
    {
        const Aabb3d tight = BrushWorldBounds(*piece.Mesh, PieceWorldTransform(world, piece));
        EXPECT_NEAR(bounds.Min.X, tight.Min.X, 1e-4f);
        EXPECT_NEAR(bounds.Max.Z, tight.Max.Z, 1e-4f);
    });
}

TEST(BrushEvaluation, ProvenanceThroughMirrorThenArray)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    const BrushEvaluated e = EvaluateBrushModifiers(
        box, { Mirror({ 1, 0, 0 }, 4.0f), Array(3, { 0, 0, 2 }) }, kPolicy);
    ASSERT_EQ(e.Pieces.size(), 6u);
    ASSERT_EQ(e.Meshes.size(), 2u);

    // Exactly one source piece, found by asking, and it is the one whose mesh is
    // the source at the identity placement.
    std::size_t sources = 0;
    for (const BrushPiece& piece : e.Pieces)
        if (piece.Origin == BrushPieceOrigin::Source)
            ++sources;
    EXPECT_EQ(sources, 1u);
    const BrushPiece& source = e.Pieces[e.SourcePiece];
    EXPECT_EQ(source.Origin, BrushPieceOrigin::Source);
    EXPECT_EQ(source.Mesh, &box);
    EXPECT_EQ(source.ProducedBy, kBrushNoModifier);
    EXPECT_TRUE(source.Placement.NearlyEquals(Transform3f::Identity(), 1e-6f));

    // Mesh lineage: the reflected mesh was minted by the Mirror (stage 0) from mesh 0.
    EXPECT_EQ(e.Meshes[0].MintedBy, kBrushNoModifier);
    EXPECT_EQ(e.Meshes[0].MintedFrom, 0u);
    EXPECT_EQ(e.Meshes[1].MintedBy, 0u);
    EXPECT_EQ(e.Meshes[1].MintedFrom, 0u);

    // Placement lineage: the mirrored piece at array step 0 says Mirror; every
    // array copy says Array, whichever mesh it draws.
    for (const BrushPiece& piece : e.Pieces)
    {
        if (piece.Origin == BrushPieceOrigin::Source)
            continue;
        const bool atStepZero = std::abs(piece.Placement.Position.Z) < 1e-6f;
        if (atStepZero)
        {
            EXPECT_EQ(piece.ProducedBy, 0u); // the Mirror
            EXPECT_EQ(piece.MeshIndex, 1u);
        }
        else
            EXPECT_EQ(piece.ProducedBy, 1u); // the Array
    }
}

TEST(BrushEvaluation, SourcePieceIsAskedNotAssumed)
{
    // Array then Mirror: the array keeps the source at ordinal 0; mirror
    // appends. Mirror then Array: same. Either way SourcePiece is the piece
    // whose Origin is Source, and consumers read it rather than 0.
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    for (const BrushModifierStack& stack :
         { BrushModifierStack{ Array(3, { 4, 0, 0 }), Mirror({ 0, 0, 1 }, 5.0f) },
           BrushModifierStack{ Mirror({ 0, 0, 1 }, 5.0f), Array(3, { 4, 0, 0 }) } })
    {
        const BrushEvaluated e = EvaluateBrushModifiers(box, stack, kPolicy);
        ASSERT_LT(e.SourcePiece, e.Pieces.size());
        EXPECT_EQ(e.Pieces[e.SourcePiece].Origin, BrushPieceOrigin::Source);
        EXPECT_EQ(e.Pieces[e.SourcePiece].Ordinal, e.SourcePiece);
    }
}

TEST(BrushEvaluation, ElementMapsAndEdgeFactsFollowTheLineage)
{
    BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    BrushSetEdgeSoft(box, box.Faces[0].Loop[0], box.Faces[0].Loop[1], true);
    const BrushEvaluated e = EvaluateBrushModifiers(box, { Mirror({ 1, 0, 0 }, 4.0f) }, kPolicy);
    ASSERT_EQ(e.Meshes.size(), 2u);
    for (const BrushEvaluatedMesh& mesh : e.Meshes)
    {
        EXPECT_EQ(mesh.ToSource.Faces, BrushElementMapKind::Identity);
        EXPECT_EQ(mesh.ToSource.Vertices, BrushElementMapKind::Identity);
        const auto pairs = BrushEdgePairs(*mesh.Mesh);
        ASSERT_EQ(mesh.EdgePairs.size(), pairs.size());
        EXPECT_EQ(mesh.EdgePairs, pairs);
        ASSERT_EQ(mesh.EdgeSoft.size(), pairs.size());
        std::size_t soft = 0;
        for (const bool s : mesh.EdgeSoft)
            soft += s ? 1 : 0;
        EXPECT_EQ(soft, 1u);
    }
    const BrushPiece& copy = e.Pieces[1];
    EXPECT_EQ(SourceFaceFor(e, copy, 3), std::optional<std::uint32_t>{ 3 });
    EXPECT_EQ(SourceVertexFor(e, copy, 5), std::optional<std::uint32_t>{ 5 });
    EXPECT_EQ(SourceEdgeFor(e, copy, 7), std::optional<std::uint32_t>{ 7 });
    EXPECT_FALSE(SourceFaceFor(e, copy, 99).has_value());
    const auto& pair = e.Meshes[0].EdgePairs[4];
    EXPECT_EQ(SourceEdgeIndexOf(e, pair[1], pair[0]), std::optional<std::uint32_t>{ 4 });
    EXPECT_FALSE(SourceEdgeIndexOf(e, 0, 7).has_value()); // opposite corners share no edge
    EXPECT_TRUE(e.LocalBounds.IsValid());
    EXPECT_NEAR(e.LocalBounds.Min.X, -9.0f, 1e-5f); // the copy across x = -4
    EXPECT_NEAR(e.LocalBounds.Max.X, 1.0f, 1e-5f);
}

TEST(BrushEvaluation, ATableMapCanNameOneSourceFromManyEvaluatedElements)
{
    // The representation, not a modifier: two evaluated faces naming one source
    // face and one naming none resolve as such.
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    BrushEvaluated e = EvaluateBrushModifiers(box, {}, kPolicy);
    BrushElementMap& map = e.Meshes[0].ToSource;
    map.Faces = BrushElementMapKind::Table;
    map.FaceTable.assign(box.Faces.size(), kBrushNoElement);
    map.FaceTable[0] = 2;
    map.FaceTable[1] = 2;
    EXPECT_EQ(SourceFaceFor(e, e.Pieces[0], 0), std::optional<std::uint32_t>{ 2 });
    EXPECT_EQ(SourceFaceFor(e, e.Pieces[0], 1), std::optional<std::uint32_t>{ 2 });
    EXPECT_FALSE(SourceFaceFor(e, e.Pieces[0], 2).has_value());
}

TEST(BrushEvaluation, MirroredFacesWindOutward)
{
    const BrushMesh box = BrushOps::MakeBox({ 2, 1, 1 });
    const BrushMesh mirrored = MirrorBrushMesh(box, Plane::FromNormalAndDistance({ 1, 0, 0 }, -4.0f));
    const Vec3d center = BrushMeshCentroid(mirrored);
    for (const BrushFace& face : mirrored.Faces)
    {
        const Vec3d normal = BrushComputeFaceNormal(mirrored, face);
        const Vec3d outward = BrushFaceCentroid(mirrored, face) - center;
        EXPECT_GT(normal.Dot(outward), 0.0f);
    }
}

TEST(BrushEvaluation, WorkCountersCountEvaluationsAndWalks)
{
    const BrushMesh box = BrushOps::MakeBox({ 1, 1, 1 });
    (void)BrushWorkCounters::TakeFrame();
    const BrushEvaluated e = EvaluateBrushModifiers(box, { Array(3, { 2, 0, 0 }) }, kPolicy);
    ForEachPieceWorldBounds(e, Transform3f::Identity(), [](const BrushPiece&, const Aabb3d&) {});
    const BrushWorkCounters frame = BrushWorkCounters::TakeFrame();
    EXPECT_EQ(frame.Evaluations, 1u);
    EXPECT_EQ(frame.PieceWalks, 1u);
    EXPECT_TRUE(BrushWorkCounters::TakeFrame().InteractiveIdle());
}
