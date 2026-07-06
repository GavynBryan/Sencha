#include "meshedit/FaceMaterialEdits.h"

#include "brush/BrushOps.h"
#include "commands/CommandStack.h"
#include "meshedit/IMeshEditTarget.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <math/Quat.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

namespace
{
// Writes the after-mesh into the live brush storage on Execute and the
// before-mesh on Undo: the same value semantics as the editor's brush edit
// command, against the fake target's storage.
class ApplyMeshCommand : public ICommand
{
public:
    ApplyMeshCommand(BrushMesh& live, BrushMesh before, BrushMesh after)
        : Live(live)
        , Before(std::move(before))
        , After(std::move(after))
    {
    }

    void Execute() override { Live = After; }
    void Undo() override { Live = Before; }

private:
    BrushMesh& Live;
    BrushMesh Before;
    BrushMesh After;
};

class FakeMeshEditTarget : public IMeshEditTarget
{
public:
    EntityId AddBrush(BrushMesh mesh, Transform3f transform)
    {
        const EntityId id{ static_cast<std::uint32_t>(Entries.size() + 1), 1 };
        Entries.push_back(Entry{ id, std::move(mesh), transform });
        return id;
    }

    [[nodiscard]] const BrushMesh& MeshOf(EntityId entity) const
    {
        for (const Entry& entry : Entries)
            if (entry.Id == entity)
                return entry.Mesh;
        ADD_FAILURE() << "unknown entity";
        static const BrushMesh empty;
        return empty;
    }

    std::optional<MeshEditTargetMesh> Resolve(EntityId entity) const override
    {
        for (const Entry& entry : Entries)
            if (entry.Id == entity)
                return MeshEditTargetMesh{ .Mesh = &entry.Mesh, .Transform = entry.Transform };
        return std::nullopt;
    }

    std::unique_ptr<ICommand> MakeEditCommand(EntityId entity, BrushMesh before, BrushMesh after) override
    {
        ++CommandsMade;
        for (Entry& entry : Entries)
            if (entry.Id == entity)
                return std::make_unique<ApplyMeshCommand>(entry.Mesh, std::move(before), std::move(after));
        return nullptr;
    }

    int CommandsMade = 0;

private:
    struct Entry
    {
        EntityId Id;
        BrushMesh Mesh;
        Transform3f Transform;
    };
    // Deque: commands hold references into entries, so storage must not move.
    std::deque<Entry> Entries;
};

class FaceMaterialEditsTest : public ::testing::Test
{
protected:
    void SelectFaces(std::initializer_list<std::pair<EntityId, std::uint32_t>> faces)
    {
        std::vector<SelectableRef> refs;
        for (const auto& [entity, face] : faces)
            refs.push_back(SelectableRef::FaceSelection(RegistryId::Global(), entity, face));
        Selection.SetSelection(std::move(refs));
    }

    SelectionContext Context;
    SelectionService Selection{ Context };
    CommandStack Commands;
    FakeMeshEditTarget Target;
};
}

TEST_F(FaceMaterialEditsTest, ApplyGroupsOneCommandPerBrush)
{
    const EntityId a = Target.AddBrush(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }), Transform3f::Identity());
    const EntityId b = Target.AddBrush(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }), Transform3f::Identity());
    SelectFaces({ { a, 0 }, { a, 2 }, { b, 1 } });

    const AssetRef material{ AssetType::Material, "asset://materials/dev/gray.smat" };
    ApplyMaterialToSelectedFaces(Target, Selection, Commands, material);

    EXPECT_EQ(Target.CommandsMade, 2);
    EXPECT_EQ(Target.MeshOf(a).Faces[0].Material.Material.Path, material.Path);
    EXPECT_EQ(Target.MeshOf(a).Faces[2].Material.Material.Path, material.Path);
    EXPECT_TRUE(Target.MeshOf(a).Faces[1].Material.Material.Path.empty());
    EXPECT_EQ(Target.MeshOf(b).Faces[1].Material.Material.Path, material.Path);
}

TEST_F(FaceMaterialEditsTest, UndoRestoresTheBeforeMesh)
{
    const EntityId a = Target.AddBrush(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }), Transform3f::Identity());
    SelectFaces({ { a, 0 } });

    ApplyMaterialToSelectedFaces(Target, Selection, Commands,
                                 AssetRef{ AssetType::Material, "asset://materials/dev/gray.smat" });
    ASSERT_FALSE(Target.MeshOf(a).Faces[0].Material.Material.Path.empty());

    Commands.Undo();
    EXPECT_TRUE(Target.MeshOf(a).Faces[0].Material.Material.Path.empty());
}

TEST_F(FaceMaterialEditsTest, CopyWithoutFaceSelectionReturnsEmpty)
{
    Target.AddBrush(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }), Transform3f::Identity());
    Selection.SetSelection({});
    EXPECT_FALSE(CopySelectedFaceProjection(Target, Selection).has_value());
}

TEST_F(FaceMaterialEditsTest, CopyPasteLandsTheSameWorldMappingAcrossTransforms)
{
    const Transform3f transformA{ Vec3d{ 2.0f, 0.0f, 0.0f }, Quat<float>::Identity(),
                                  Vec3d{ 1.0f, 1.0f, 1.0f } };
    const Transform3f transformB{ Vec3d{ 0.0f, 3.0f, -1.0f },
                                  Quat<float>::FromAxisAngle(Vec3d{ 0.0f, 1.0f, 0.0f },
                                                             std::numbers::pi_v<float> / 2.0f),
                                  Vec3d{ 1.0f, 1.0f, 1.0f } };

    BrushMesh meshA = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    meshA.Faces[0].Material.Material = AssetRef{ AssetType::Material, "asset://materials/dev/brick.smat" };
    meshA.Faces[0].Material.Uv.Scale = { 0.5f, 0.25f };
    meshA.Faces[0].Material.Uv.Offset = { 0.1f, 0.2f };
    meshA.Faces[0].Material.Uv.Rotation = 30.0f;

    const EntityId a = Target.AddBrush(std::move(meshA), transformA);
    const EntityId b = Target.AddBrush(BrushOps::MakeBox({ 1.0f, 2.0f, 1.0f }), transformB);

    SelectFaces({ { a, 0 } });
    const std::optional<FaceMaterialClipboard> clipboard = CopySelectedFaceProjection(Target, Selection);
    ASSERT_TRUE(clipboard.has_value());
    EXPECT_EQ(clipboard->Material.Path, "asset://materials/dev/brick.smat");

    SelectFaces({ { b, 3 } });
    PasteFaceProjection(Target, Selection, Commands, *clipboard);

    const BrushMesh& pastedMesh = Target.MeshOf(b);
    const BrushFace& pasted = pastedMesh.Faces[3];
    EXPECT_EQ(pasted.Material.Material.Path, "asset://materials/dev/brick.smat");

    // The bridge invariant: the pasted brush-local projection reproduces the
    // copied WORLD mapping at every vertex of the target face, so the texture
    // lands identically despite the different brush transforms.
    for (const std::uint32_t index : pasted.Loop)
    {
        const Vec3d local = pastedMesh.Vertices[index].Position;
        const Vec2d viaLocal = ProjectUv(pasted.Material.Uv, local);
        const Vec2d viaWorld = ProjectWorldUv(clipboard->Projection, transformB.TransformPoint(local));
        EXPECT_NEAR(viaLocal.X, viaWorld.X, 1e-3);
        EXPECT_NEAR(viaLocal.Y, viaWorld.Y, 1e-3);
    }
}
