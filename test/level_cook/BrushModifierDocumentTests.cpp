// Modifier stacks through the document layer: the brush record (mesh + stack)
// travels as one unit through the sidecar, snapshots, duplication, instancing,
// prefab projection, and the brush commands, and the cook consumes what the
// stack evaluates to.

#include "brush/BrushBounds.h"
#include "brush/BrushEvaluation.h"
#include "brush/BrushOps.h"
#include "document/BrushCookInput.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/DetachSharedBrushCommand.h"
#include "document/commands/DuplicateEntitiesCommand.h"
#include "document/commands/MergeBrushesCommand.h"
#include "document/commands/SeparateFacesCommand.h"
#include "document/commands/SetBrushOriginCommand.h"
#include "document/commands/ValueCommand.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/identity/Id.h>
#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentIdComponent.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

    std::size_t PieceCount(const EditorScene& scene, EntityId entity)
    {
        const BrushEvaluated* e = scene.TryGetBrushPieces(entity);
        return e != nullptr ? e->Pieces.size() : 0;
    }

    // Every world-space triangle vertex of a collected brush set, sorted, so two
    // collections compare independent of brush or piece order.
    std::vector<Vec3d> SortedVertices(const std::vector<CookBrushGeometry>& brushes)
    {
        std::vector<Vec3d> out;
        for (const CookBrushGeometry& brush : brushes)
            for (const CookFace& face : brush.Faces)
                for (const StaticMeshVertex& v : face.Triangles)
                    out.push_back(v.Position);
        std::sort(out.begin(), out.end(), [](Vec3d a, Vec3d b) {
            if (a.X != b.X) return a.X < b.X;
            if (a.Y != b.Y) return a.Y < b.Y;
            return a.Z < b.Z;
        });
        return out;
    }

    class BrushModifierDocumentTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        [[nodiscard]] static PersistentEntityId IdOf(const EditorDocument& doc, EntityId entity)
        {
            const auto* id = doc.GetRegistry().Components.TryGet<PersistentIdComponent>(entity);
            return id != nullptr ? id->Id : PersistentEntityId{};
        }

        [[nodiscard]] static EntityId FindById(const EditorDocument& doc, PersistentEntityId id)
        {
            for (EntityId entity : doc.GetScene().GetAllEntities())
                if (IdOf(doc, entity) == id)
                    return entity;
            return {};
        }

        EntityId Instantiate(EntityId source)
        {
            const std::array<EntityId, 1> sources = { source };
            const std::array<Transform3f, 1> transforms = { *Scene.TryGetWorldTransform(source) };
            DuplicateEntitiesCommand command(sources, transforms, Scene, Document, Selection,
                                             DuplicateBranchPolicy::Subtree, /*asInstance*/ true);
            command.Execute();
            return Selection.GetPrimarySelection().Entity;
        }

        LoggingProvider  Logging;
        EditorDocument   Document{ Logging };
        EditorScene&     Scene = Document.GetScene();
        SelectionContext Context;
        SelectionService Selection{ Context };
    };
}

TEST_F(BrushModifierDocumentTest, StackRoundTripsThroughSceneTextAndSavesStably)
{
    const EntityId brush = Scene.CreateBrush({ 1, 0, 0 }, { 1, 1, 1 });
    Scene.SetBrushModifiers(brush, { Mirror({ 1, 0, 0 }, -2.0f), Array(3, { 0, 0, 4 }) });
    const PersistentEntityId id = IdOf(Document, brush);

    const std::string text = Document.ToSceneText();
    EXPECT_NE(text.find("modifiers"), std::string::npos);

    EditorDocument loaded(Logging);
    ASSERT_TRUE(loaded.LoadFromSceneText(text));
    const EntityId back = FindById(loaded, id);
    ASSERT_TRUE(back.IsValid());
    const BrushModifierStack* stack = loaded.GetScene().TryGetBrushModifiers(back);
    ASSERT_NE(stack, nullptr);
    ASSERT_EQ(stack->size(), 2u);
    EXPECT_TRUE(std::holds_alternative<MirrorModifier>((*stack)[0].Params));
    EXPECT_EQ(std::get<ArrayModifier>((*stack)[1].Params).Count, 3);
    EXPECT_EQ(PieceCount(loaded.GetScene(), back), 6u);
    EXPECT_EQ(loaded.ToSceneText(), text);
}

TEST_F(BrushModifierDocumentTest, ABrushWithoutModifiersSavesExactlyAsBefore)
{
    const EntityId brush = Scene.CreateBrush({ 1, 0, 0 }, { 1, 1, 1 });
    (void)brush;
    EXPECT_EQ(Document.ToSceneText().find("modifiers"), std::string::npos);
}

TEST_F(BrushModifierDocumentTest, StackEditCommandUndoesAndRedoesOnlyItself)
{
    const EntityId brush = Scene.CreateBrush({}, { 1, 1, 1 });
    auto edit = MakeEditBrushModifiersCommand(brush, {}, { Array(4, { 3, 0, 0 }) }, Scene, Document);
    edit->Execute();
    EXPECT_EQ(PieceCount(Scene, brush), 4u);

    // A mesh edit in between must survive the stack undo.
    Scene.SetBrushMesh(brush, BrushOps::MakeBox({ 2, 2, 2 }));
    edit->Undo();
    EXPECT_EQ(PieceCount(Scene, brush), 1u);
    EXPECT_NEAR(BrushComputeBounds(*Scene.TryGetBrushMesh(brush)).Max.X, 2.0f, 1e-5f);
    edit->Execute();
    EXPECT_EQ(PieceCount(Scene, brush), 4u);
    EXPECT_NEAR(BrushComputeBounds(*Scene.TryGetBrushMesh(brush)).Max.X, 2.0f, 1e-5f);
}

TEST_F(BrushModifierDocumentTest, SourceEditPropagatesThroughTheStack)
{
    const EntityId brush = Scene.CreateBrush({}, { 1, 1, 1 });
    Scene.SetBrushModifiers(brush, { Array(3, { 5, 0, 0 }) });
    const std::uint64_t before = Scene.GetBrushMeshStore().FindRecord(Scene.TryGetBrush(brush)->Id)->Revision;
    ASSERT_EQ(PieceCount(Scene, brush), 3u);

    Scene.SetBrushMesh(brush, BrushOps::MakeBox({ 1, 3, 1 }));
    const std::uint64_t after = Scene.GetBrushMeshStore().FindRecord(Scene.TryGetBrush(brush)->Id)->Revision;
    EXPECT_EQ(after, before + 1);
    const BrushEvaluated* e = Scene.TryGetBrushPieces(brush);
    ASSERT_EQ(e->Pieces.size(), 3u);
    for (const BrushPiece& piece : e->Pieces)
        EXPECT_NEAR(BrushComputeBounds(*piece.Mesh).Max.Y, 3.0f, 1e-5f);
}

TEST_F(BrushModifierDocumentTest, DuplicateCopiesInstanceSharesMakeUniqueCopiesAgain)
{
    const EntityId source = Scene.CreateBrush({}, { 1, 1, 1 });
    Scene.SetBrushModifiers(source, { Mirror({ 0, 1, 0 }) });

    const EntityId copy = Document.DuplicateEntity(source);
    ASSERT_NE(Scene.TryGetBrush(copy)->Id, Scene.TryGetBrush(source)->Id);
    EXPECT_EQ(PieceCount(Scene, copy), 2u);
    Scene.SetBrushModifiers(copy, {});
    EXPECT_EQ(PieceCount(Scene, source), 2u); // independent

    const EntityId instance = Instantiate(source);
    EXPECT_EQ(Scene.TryGetBrush(instance)->Id, Scene.TryGetBrush(source)->Id);
    Scene.SetBrushModifiers(instance, { Mirror({ 0, 1, 0 }), Array(2, { 4, 0, 0 }) });
    EXPECT_EQ(PieceCount(Scene, source), 4u); // shared

    auto detach = MakeDetachSharedBrushCommand(Scene, Document, instance);
    ASSERT_NE(detach, nullptr);
    detach->Execute();
    EXPECT_EQ(PieceCount(Scene, instance), 4u); // took the live stack along
    Scene.SetBrushModifiers(instance, {});
    EXPECT_EQ(PieceCount(Scene, source), 4u);
}

TEST_F(BrushModifierDocumentTest, SnapshotCarriesTheRecordAtomically)
{
    const EntityId brush = Scene.CreateBrush({ 2, 0, 0 }, { 1, 1, 1 });
    Scene.SetBrushModifiers(brush, { Array(5, { 0, 2, 0 }) });
    const BrushId id = Scene.TryGetBrush(brush)->Id;

    const EntitySnapshot snapshot = Document.CaptureEntity(brush);
    ASSERT_TRUE(snapshot.Brush.has_value());
    EXPECT_EQ(snapshot.Brush->Modifiers.size(), 1u);
    Scene.DestroyEntity(brush);
    EXPECT_EQ(Scene.GetBrushMeshStore().FindRecord(id), nullptr);

    const EntityId restored = Document.RestoreEntity(snapshot);
    EXPECT_EQ(Scene.TryGetBrush(restored)->Id, id);
    EXPECT_EQ(PieceCount(Scene, restored), 5u);
}

TEST_F(BrushModifierDocumentTest, ReoriginLeavesACustomMirrorInPlace)
{
    const EntityId brush = Scene.CreateBrush({ 1, 0, 0 }, { 1, 1, 1 });
    Scene.SetBrushModifiers(brush, { Mirror({ 1, 0, 0 }, -3.0f) }); // Custom plane
    const Aabb3d before = *Scene.EvaluatedWorldBounds(brush);

    auto command = MakeSetBrushOriginCommand(Scene, brush, Vec3d{ 4, 1, 0 });
    ASSERT_NE(command, nullptr);
    command->Execute();
    const Aabb3d after = *Scene.EvaluatedWorldBounds(brush);
    EXPECT_NEAR(before.Min.X, after.Min.X, 1e-4f);
    EXPECT_NEAR(before.Max.X, after.Max.X, 1e-4f);
    EXPECT_NEAR(before.Min.Y, after.Min.Y, 1e-4f);
    EXPECT_NEAR(before.Max.Y, after.Max.Y, 1e-4f);

    command->Undo();
    const Aabb3d undone = *Scene.EvaluatedWorldBounds(brush);
    EXPECT_NEAR(before.Min.X, undone.Min.X, 1e-4f);
    EXPECT_NEAR(before.Max.X, undone.Max.X, 1e-4f);
}

TEST_F(BrushModifierDocumentTest, MergeFlattensEveryParticipantIntoAnEmptyStack)
{
    const EntityId target = Scene.CreateBrush({ 0, 0, 0 }, { 1, 1, 1 });
    Scene.SetBrushModifiers(target, { Array(2, { 4, 0, 0 }) });
    const EntityId source = Scene.CreateBrush({ 0, 10, 0 }, { 1, 1, 1 });
    Scene.SetBrushModifiers(source, { Mirror({ 0, 1, 0 }, 5.0f) }); // local y = -5 (world y = 5): copy lands at world y in [-1, 1]

    const std::array<EntityId, 1> sources = { source };
    auto command = MakeMergeBrushesCommand(target, sources, Scene, Document, Selection);
    ASSERT_NE(command, nullptr);
    command->Execute();

    EXPECT_TRUE(Scene.TryGetBrushModifiers(target)->empty());
    EXPECT_EQ(Scene.TryGetBrushMesh(target)->Faces.size(), 24u); // 2 + 2 boxes
    EXPECT_EQ(PieceCount(Scene, target), 1u);
    const Aabb3d bounds = *Scene.EvaluatedWorldBounds(target);
    EXPECT_NEAR(bounds.Max.X, 5.0f, 1e-4f);
    EXPECT_NEAR(bounds.Max.Y, 11.0f, 1e-4f);
    EXPECT_NEAR(bounds.Min.Y, -1.0f, 1e-4f);
    EXPECT_NEAR(bounds.Min.X, -1.0f, 1e-4f);

    command->Undo();
    ASSERT_EQ(Scene.TryGetBrushModifiers(target)->size(), 1u);
    EXPECT_EQ(Scene.TryGetBrushMesh(target)->Faces.size(), 6u);
    EXPECT_EQ(PieceCount(Scene, target), 2u);
}

TEST_F(BrushModifierDocumentTest, SeparatedFacesKeepTheStack)
{
    const EntityId source = Scene.CreateBrush({}, { 1, 1, 1 });
    Scene.SetBrushModifiers(source, { Mirror({ 1, 0, 0 }, -3.0f) });
    const std::array<std::uint32_t, 1> faces = { 0 };
    auto command = MakeSeparateFacesCommand(source, faces, Scene, Document, Selection);
    ASSERT_NE(command, nullptr);
    command->Execute();
    const EntityId created = Selection.GetPrimarySelection().Entity;
    ASSERT_TRUE(created.IsValid());
    ASSERT_NE(Scene.TryGetBrushModifiers(created), nullptr);
    EXPECT_EQ(Scene.TryGetBrushModifiers(created)->size(), 1u);
    EXPECT_EQ(PieceCount(Scene, created), 2u);
}

TEST_F(BrushModifierDocumentTest, SourceAndEvaluatedBoundsAreDistinct)
{
    const EntityId brush = Scene.CreateBrush({}, { 1, 1, 1 });
    Scene.SetBrushModifiers(brush, { Array(3, { 4, 0, 0 }) });
    EXPECT_NEAR(Scene.SourceWorldBounds(brush)->Max.X, 1.0f, 1e-5f);
    EXPECT_NEAR(Scene.EvaluatedWorldBounds(brush)->Max.X, 9.0f, 1e-5f);
}

TEST_F(BrushModifierDocumentTest, CookCollectsPiecesLikeHandPlacedBrushes)
{
    const EntityId arrayed = Scene.CreateBrush({ 0, 0, 0 }, { 1, 1, 1 });
    Scene.SetBrushModifiers(arrayed, { Array(3, { 4, 0, 0 }) });
    const std::vector<CookBrushGeometry> viaModifier =
        CollectCookBrushes(Scene, Document.GetDefaultMaterial());
    EXPECT_EQ(viaModifier.size(), 3u);

    EditorDocument other(Logging);
    for (int i = 0; i < 3; ++i)
        (void)other.GetScene().CreateBrush({ 4.0f * i, 0, 0 }, { 1, 1, 1 });
    const std::vector<CookBrushGeometry> byHand =
        CollectCookBrushes(other.GetScene(), other.GetDefaultMaterial());
    ASSERT_EQ(byHand.size(), 3u);

    const std::vector<Vec3d> a = SortedVertices(viaModifier);
    const std::vector<Vec3d> b = SortedVertices(byHand);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_NEAR(a[i].X, b[i].X, 1e-5f);
        EXPECT_NEAR(a[i].Y, b[i].Y, 1e-5f);
        EXPECT_NEAR(a[i].Z, b[i].Z, 1e-5f);
    }
}

TEST_F(BrushModifierDocumentTest, CookReportsAStackPastTheHardLimitAndIgnoresThePreviewBudget)
{
    const EntityId brush = Scene.CreateBrush({}, { 1, 1, 1 });
    Scene.SetBrushModifiers(brush, { Array(1000, { 1, 0, 0 }), Array(1000, { 0, 1, 0 }) });
    std::vector<CookBrushFailure> failures;
    (void)CollectCookBrushes(Scene, Document.GetDefaultMaterial(), nullptr, 45.0f, 0.25f,
                             BrushEvaluationPolicy::Cook(), &failures);
    ASSERT_EQ(failures.size(), 1u);
    EXPECT_EQ(failures[0].Entity, brush);
    EXPECT_EQ(failures[0].Modifier, 1u);

    // Within the hard limit but past a tiny preview budget: the cook is unaffected.
    Scene.SetBrushModifiers(brush, { Array(50, { 1, 0, 0 }) });
    Scene.SetInteractiveEvaluationPolicy(BrushEvaluationPolicy::Interactive(4));
    EXPECT_EQ(PieceCount(Scene, brush), 1u); // preview stopped before the array
    failures.clear();
    const std::vector<CookBrushGeometry> cooked = CollectCookBrushes(
        Scene, Document.GetDefaultMaterial(), nullptr, 45.0f, 0.25f,
        BrushEvaluationPolicy::Cook(), &failures);
    EXPECT_TRUE(failures.empty());
    EXPECT_EQ(cooked.size(), 50u);
}

TEST_F(BrushModifierDocumentTest, APrefabMemberProjectsWithItsStack)
{
    const fs::path root = fs::temp_directory_path()
        / ("sencha_modifier_prefab_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::remove_all(root);
    fs::create_directories(root / "props");

    EditorDocument prop(Logging);
    const EntityId body = prop.GetScene().CreateBrush(Vec3d{ 0, 1, 0 });
    prop.GetScene().SetBrushModifiers(body, { Array(3, { 0, 0, 2 }) });
    ASSERT_TRUE(prop.SaveAs((root / "props/rail.sscene").generic_string()));
    const PersistentEntityId bodyId = IdOf(prop, body);

    const std::string host = std::string(R"({
  format_version: 1,
  entities: [],
  instances: [
    { id: '00000000000000f0',
      source: 'asset://props/rail.sscene',
      transform: { position: [5, 0, 0], rotation: [0, 0, 0, 1], scale: [1, 1, 1] },
      entity_ids: { ')") + PersistentEntityIdToString(bodyId) + R"(': '0000000000000201' } },
  ],
})";
    EditorDocument hostDoc(Logging);
    hostDoc.SetContentRoots({ root });
    ASSERT_TRUE(hostDoc.LoadFromSceneText(host));
    const EntityId member = FindById(hostDoc, PersistentEntityId{ 0x201 });
    ASSERT_TRUE(member.IsValid());
    EXPECT_EQ(PieceCount(hostDoc.GetScene(), member), 3u);

    std::error_code ec;
    fs::remove_all(root, ec);
}

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

    BrushModifier BoundsArray(LocalAxis axis, int count, float gap)
    {
        BrushModifier m;
        ArrayModifier array;
        array.Axis = axis;
        array.Count = count;
        array.Spacing = gap;
        m.Params = array;
        return m;
    }

    // World-space AABB of one evaluated piece.
    Aabb3d PieceWorldBounds(const EditorScene& scene, EntityId entity, std::size_t ordinal)
    {
        const BrushEvaluated* e = scene.TryGetBrushPieces(entity);
        const Transform3f* world = scene.TryGetWorldTransform(entity);
        EXPECT_NE(e, nullptr);
        EXPECT_LT(ordinal, e->Pieces.size());
        const BrushPiece& piece = e->Pieces[ordinal];
        return BrushWorldBounds(*piece.Mesh, PieceWorldTransform(*world, piece));
    }
}

TEST_F(BrushModifierDocumentTest, AnOriginMirrorFollowsTheOriginImmediatelyAndThroughUndo)
{
    // A wall 1 wide standing at x in [4, 6] world, origin at x = 5.
    const EntityId brush = Scene.CreateBrush({ 5, 0, 0 }, { 1, 1, 1 });
    Scene.SetBrushModifiers(brush, { OriginMirror(LocalAxis::X) });
    const Aabb3d sourceBefore = PieceWorldBounds(Scene, brush, 0);
    const Aabb3d copyBefore = PieceWorldBounds(Scene, brush, 1);
    EXPECT_NEAR(copyBefore.Min.X, 4.0f, 1e-4f); // mirrored onto itself: plane through its middle

    // Move the origin 3 to the left: the plane goes with it, the copy lands 2*3 further.
    auto command = MakeSetBrushOriginCommand(Scene, brush, Vec3d{ 2, 0, 0 });
    ASSERT_NE(command, nullptr);
    command->Execute();
    const Aabb3d sourceAfter = PieceWorldBounds(Scene, brush, 0);
    const Aabb3d copyAfter = PieceWorldBounds(Scene, brush, 1);
    EXPECT_NEAR(sourceAfter.Min.X, sourceBefore.Min.X, 1e-4f);
    EXPECT_NEAR(copyAfter.Min.X, copyBefore.Min.X - 6.0f, 1e-4f);
    EXPECT_NEAR(copyAfter.Max.X, copyBefore.Max.X - 6.0f, 1e-4f);

    command->Undo();
    EXPECT_NEAR(PieceWorldBounds(Scene, brush, 1).Min.X, copyBefore.Min.X, 1e-4f);
    command->Execute();
    EXPECT_NEAR(PieceWorldBounds(Scene, brush, 1).Min.X, copyBefore.Min.X - 6.0f, 1e-4f);
}

TEST_F(BrushModifierDocumentTest, ABoundsCenterMirrorNeedsNoRebaseAcrossReorigin)
{
    const EntityId brush = Scene.CreateBrush({ 5, 0, 0 }, { 1, 1, 1 });
    BrushModifier mirror = OriginMirror(LocalAxis::X, 2.0f);
    std::get<MirrorModifier>(mirror.Params).Source = MirrorPlaneSource::BoundsCenter;
    Scene.SetBrushModifiers(brush, { mirror });
    const Aabb3d before = PieceWorldBounds(Scene, brush, 1);
    auto command = MakeSetBrushOriginCommand(Scene, brush, Vec3d{ 1, 1, 0 });
    ASSERT_NE(command, nullptr);
    command->Execute();
    const Aabb3d after = PieceWorldBounds(Scene, brush, 1);
    EXPECT_NEAR(before.Min.X, after.Min.X, 1e-4f);
    EXPECT_NEAR(before.Max.X, after.Max.X, 1e-4f);
}

TEST_F(BrushModifierDocumentTest, ABoundsArrayKeepsTouchingWhenTheSourceWidens)
{
    const EntityId brush = Scene.CreateBrush({}, { 64, 1, 1 }); // 128 wide
    Scene.SetBrushModifiers(brush, { BoundsArray(LocalAxis::X, 5, 0.0f) });
    ASSERT_EQ(PieceCount(Scene, brush), 5u);
    for (std::size_t i = 1; i < 5; ++i)
        EXPECT_NEAR(PieceWorldBounds(Scene, brush, i).Min.X, PieceWorldBounds(Scene, brush, i - 1).Max.X, 1e-3f);

    Scene.SetBrushMesh(brush, BrushOps::MakeBox({ 96, 1, 1 })); // 192 wide
    for (std::size_t i = 1; i < 5; ++i)
        EXPECT_NEAR(PieceWorldBounds(Scene, brush, i).Min.X, PieceWorldBounds(Scene, brush, i - 1).Max.X, 1e-3f);
    EXPECT_NEAR(Scene.EvaluatedWorldBounds(brush)->Max.X, 96.0f + 4 * 192.0f, 1e-2f);

    Scene.SetBrushModifiers(brush, { BoundsArray(LocalAxis::X, 5, 16.0f) });
    for (std::size_t i = 1; i < 5; ++i)
        EXPECT_NEAR(PieceWorldBounds(Scene, brush, i).Min.X - PieceWorldBounds(Scene, brush, i - 1).Max.X,
                    16.0f, 1e-3f);
}

TEST_F(BrushModifierDocumentTest, ABoundsArrayMarchesAlongTheEntitysOwnAxis)
{
    const EntityId brush = Scene.CreateBrush({}, { 64, 1, 1 });
    Scene.SetBrushModifiers(brush, { BoundsArray(LocalAxis::X, 3, 16.0f) });
    Transform3f turned = *Scene.TryGetLocalTransform(brush);
    turned.Rotation = Quatf::FromAxisAngle(Vec3d{ 0, 1, 0 }, 3.14159265f * 0.5f);
    Scene.SetTransform(brush, turned);
    Scene.RefreshDerivedTransforms();

    // Local +X now points along world -Z: copies line up along Z, still 16 apart.
    const Aabb3d a = PieceWorldBounds(Scene, brush, 0);
    const Aabb3d b = PieceWorldBounds(Scene, brush, 1);
    EXPECT_NEAR(a.Min.X, b.Min.X, 1e-3f);
    EXPECT_NEAR(std::abs(b.Center().Z - a.Center().Z), 128.0f + 16.0f, 1e-2f);
    EXPECT_NEAR(std::min(std::abs(b.Min.Z - a.Max.Z), std::abs(a.Min.Z - b.Max.Z)), 16.0f, 1e-2f);
}

TEST_F(BrushModifierDocumentTest, EvaluatedBoundsAreTheVertexTightUnionUnderRotation)
{
    const EntityId brush = Scene.CreateBrush({}, { 4, 1, 1 });
    Scene.SetBrushModifiers(brush, { BoundsArray(LocalAxis::X, 4, 2.0f) });
    Transform3f turned = *Scene.TryGetLocalTransform(brush);
    turned.Rotation = Quatf::FromAxisAngle(Vec3d{ 0, 1, 0 }, 0.6f) * Quatf::FromAxisAngle(Vec3d{ 1, 0, 0 }, 0.3f);
    turned.Scale = { 1.5f, 1.0f, 0.5f };
    Scene.SetTransform(brush, turned);
    Scene.RefreshDerivedTransforms();

    // The bounds a caller sees are exactly the union of every piece's
    // transformed vertices, not a box re-bounded from a rotated box.
    Aabb3d expected = Aabb3d::Empty();
    const BrushEvaluated* evaluated = Scene.TryGetBrushPieces(brush);
    const Transform3f& world = *Scene.TryGetWorldTransform(brush);
    for (const BrushPiece& piece : evaluated->Pieces)
        for (const BrushVertex& vertex : piece.Mesh->Vertices)
            expected.ExpandToInclude(PieceWorldTransform(world, piece).TransformPoint(vertex.Position));
    const Aabb3d bounds = *Scene.EvaluatedWorldBounds(brush);
    EXPECT_NEAR(bounds.Min.X, expected.Min.X, 1e-3f);
    EXPECT_NEAR(bounds.Min.Y, expected.Min.Y, 1e-3f);
    EXPECT_NEAR(bounds.Min.Z, expected.Min.Z, 1e-3f);
    EXPECT_NEAR(bounds.Max.X, expected.Max.X, 1e-3f);
    EXPECT_NEAR(bounds.Max.Y, expected.Max.Y, 1e-3f);
    EXPECT_NEAR(bounds.Max.Z, expected.Max.Z, 1e-3f);
}
