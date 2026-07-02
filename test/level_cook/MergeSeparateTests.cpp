// Merge (join shells) and separate-faces commands: world geometry and texture
// placement survive the rebase, undo restores both sides exactly.

#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/DocumentSerialization.h"
#include "document/commands/MergeBrushesCommand.h"
#include "document/commands/SeparateFacesCommand.h"
#include "brush/BrushTransform.h"
#include "brush/FaceMaterial.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{
    class MergeSeparateTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        LoggingProvider  Logging;
        EditorDocument   Document{ Logging };
        EditorScene&     Scene = Document.GetScene();
        SelectionContext Context;
        SelectionService Selection{ Context };
    };

    // World positions of every vertex, order-insensitive containment check.
    bool ContainsWorldPoint(const BrushMesh& mesh, const Transform3f& t, Vec3d world)
    {
        for (const BrushVertex& v : mesh.Vertices)
            if ((t.TransformPoint(v.Position) - world).SqrMagnitude() < 1e-6f)
                return true;
        return false;
    }
}

TEST_F(MergeSeparateTest, MergeJoinsShellsWithWorldPositionsIntact)
{
    const EntityId target = Scene.CreateBrush({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
    const EntityId source = Scene.CreateBrush({ 5.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });

    const std::array<EntityId, 1> sources = { source };
    auto command = MakeMergeBrushesCommand(target, sources, Scene, Document, Selection);
    ASSERT_NE(command, nullptr);
    command->Execute();

    EXPECT_FALSE(Scene.HasEntity(source));
    const BrushMesh* merged = Scene.TryGetBrushMesh(target);
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->Faces.size(), 12u);
    // A source corner (5,0,0)+(1,1,1) = (6,1,1) lands at the same world spot.
    EXPECT_TRUE(ContainsWorldPoint(*merged, *Scene.TryGetTransform(target), { 6.0f, 1.0f, 1.0f }));

    command->Undo();
    EXPECT_EQ(Scene.GetEntityCount(), 2u);
    EXPECT_EQ(Scene.TryGetBrushMesh(target)->Faces.size(), 6u);
}

TEST_F(MergeSeparateTest, MergePreservesWorldTexturePlacementAcrossRotatedFrames)
{
    const EntityId target = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
    const EntityId source = Scene.CreateBrush({ 3.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });

    // Rotate the source's frame so a straight UV copy would shift the texture.
    Transform3f rotated = *Scene.TryGetTransform(source);
    rotated.Rotation = Quatf::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, 0.7f);
    Scene.SetTransform(source, rotated);

    // Sample the source's first face UV at its first vertex, in world terms.
    const BrushMesh* sourceMesh = Scene.TryGetBrushMesh(source);
    const BrushFace& face = sourceMesh->Faces[0];
    const Vec3d sampleLocal = sourceMesh->Vertices[face.Loop[0]].Position;
    const Vec2d uvBefore = ProjectUv(face.Material.Uv, sampleLocal);
    const Vec3d sampleWorld = rotated.TransformPoint(sampleLocal);

    const std::array<EntityId, 1> sources = { source };
    auto command = MakeMergeBrushesCommand(target, sources, Scene, Document, Selection);
    ASSERT_NE(command, nullptr);
    command->Execute();

    // Find the merged vertex at the same world position and compare its UV
    // under the rebased projection: identical placement.
    const BrushMesh* merged = Scene.TryGetBrushMesh(target);
    const Transform3f targetTransform = *Scene.TryGetTransform(target);
    bool found = false;
    for (const BrushFace& mergedFace : merged->Faces)
        for (std::uint32_t index : mergedFace.Loop)
        {
            const Vec3d local = merged->Vertices[index].Position;
            if ((targetTransform.TransformPoint(local) - sampleWorld).SqrMagnitude() > 1e-6f)
                continue;
            // Same world normal family as the sampled face? Compare UVs only on
            // the face that came from the source's face 0 (its projection).
            const Vec2d uvAfter = ProjectUv(mergedFace.Material.Uv, local);
            if (std::abs(uvAfter.X - uvBefore.X) < 1e-3f && std::abs(uvAfter.Y - uvBefore.Y) < 1e-3f)
                found = true;
        }
    EXPECT_TRUE(found);
}

TEST_F(MergeSeparateTest, MergingAnInstancedSourceLeavesSiblingsAlive)
{
    const EntityId target = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
    const EntityId source = Scene.CreateBrush({ 4.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
    const BrushId sharedId = Scene.TryGetBrush(source)->Id;

    // Sibling instance of the source.
    const EntityId sibling = Document.RestoreEntity(Document.CaptureEntity(source), /*freshMesh*/ false);
    ASSERT_TRUE(Scene.TryGetBrush(sibling)->Id == sharedId);

    const std::array<EntityId, 1> sources = { source };
    auto command = MakeMergeBrushesCommand(target, sources, Scene, Document, Selection);
    ASSERT_NE(command, nullptr);
    command->Execute();

    EXPECT_TRUE(Scene.HasEntity(sibling));
    EXPECT_NE(Scene.TryGetBrushMesh(sibling), nullptr); // shared mesh survived
}

TEST_F(MergeSeparateTest, SeparateSplitsFacesIntoANewBrush)
{
    const EntityId source = Scene.CreateBrush({ 1.0f, 2.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });
    const Transform3f transform = *Scene.TryGetTransform(source);

    // Take faces 0 and 1 out.
    const std::array<std::uint32_t, 2> faces = { 0u, 1u };
    const Vec3d worldProbe = transform.TransformPoint(
        Scene.TryGetBrushMesh(source)->Vertices[Scene.TryGetBrushMesh(source)->Faces[0].Loop[0]].Position);

    auto command = MakeSeparateFacesCommand(source, faces, Scene, Document, Selection);
    ASSERT_NE(command, nullptr);
    command->Execute();

    EXPECT_EQ(Scene.GetEntityCount(), 2u);
    EXPECT_EQ(Scene.TryGetBrushMesh(source)->Faces.size(), 4u);
    const EntityId created = Selection.GetPrimarySelection().Entity;
    ASSERT_TRUE(Scene.HasEntity(created));
    const BrushMesh* separated = Scene.TryGetBrushMesh(created);
    ASSERT_NE(separated, nullptr);
    EXPECT_EQ(separated->Faces.size(), 2u);
    // Same transform, same world positions.
    EXPECT_TRUE(ContainsWorldPoint(*separated, *Scene.TryGetTransform(created), worldProbe));

    command->Undo();
    EXPECT_EQ(Scene.GetEntityCount(), 1u);
    EXPECT_EQ(Scene.TryGetBrushMesh(source)->Faces.size(), 6u);
}

TEST_F(MergeSeparateTest, SeparateRefusesToEmptyTheSource)
{
    const EntityId source = Scene.CreateBrush({}, { 1.0f, 1.0f, 1.0f });
    const std::array<std::uint32_t, 6> all = { 0u, 1u, 2u, 3u, 4u, 5u };
    EXPECT_EQ(MakeSeparateFacesCommand(source, all, Scene, Document, Selection), nullptr);
}
