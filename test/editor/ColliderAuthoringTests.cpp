// A collider authored on an entity the way a designer does it: drawn where the
// body will hold it, its rows the ones its shape has, resized by handles, fitted
// to the entity's mesh, and scaled with the entity -- every edit undoable.

#include "WorkspaceFixture.h"

#include "authoring/ColliderEditorAdapter.h"
#include "authoring/ComponentPropertySource.h"
#include "document/EditorDocument.h"

#include <physics/components/Collider.h>

#include <cstring>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
class ColliderAuthoringTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        Inspector = std::make_unique<ComponentPropertySource>(Workspace.World, Workspace.Selection,
                                                              Workspace.Commands, Workspace.Affordances->Registry());
    }

    ::World& Entities() { return Scene().GetRegistry().Components; }

    EntityId Crate(Vec3d scale = Vec3d::One())
    {
        const EntityId entity = Scene().CreateEntity(Vec3d(0.0f, 0.0f, 0.0f));
        Transform3f transform = *Scene().TryGetLocalTransform(entity);
        transform.Scale = scale;
        Scene().SetTransform(entity, transform);
        SelectEntity(entity);
        return entity;
    }

    const Collider& ColliderOn(EntityId entity) { return *Entities().TryGet<Collider>(entity); }

    static const PropertyNode* Find(const std::vector<PropertyNode>& nodes, std::string_view key)
    {
        for (const PropertyNode& node : nodes)
        {
            if (node.Key == key)
                return &node;
            if (const PropertyNode* child = Find(node.Children, key))
                return child;
        }
        return nullptr;
    }

    EditorComponentInspectorContext Context(EntityId entity)
    {
        return EditorComponentInspectorContext{ .World = Workspace.World,
                                                .Document = Workspace.ActiveDocument(),
                                                .Scene = Scene(),
                                                .Selection = Workspace.Selection,
                                                .Commands = Workspace.Commands,
                                                .Entity = entity };
    }

    std::unique_ptr<ComponentPropertySource> Inspector;
};
} // namespace

TEST_F(ColliderAuthoringTest, AColliderShowsTheRowsItsShapeHasAndEachEditIsAStep)
{
    const EntityId crate = Crate();
    ASSERT_EQ(Inspector->Act("components", "add", "collider"), PropertyEditResult::Applied);
    ASSERT_TRUE(Entities().HasComponent<Collider>(crate));

    std::vector<PropertyNode> rows = Inspector->Nodes();
    ASSERT_NE(Find(rows, "collider:shape"), nullptr);
    EXPECT_EQ(Find(rows, "collider:shape")->Value, "Box");
    ASSERT_NE(Find(rows, "collider:size"), nullptr);
    EXPECT_EQ(Find(rows, "collider:radius"), nullptr) << "a box offered a radius";
    EXPECT_EQ(Find(rows, "collider:shape/half_extents"), nullptr) << "the raw shape fields were shown too";

    ASSERT_EQ(Inspector->CommitEdit("collider:size", "2, 4, 6"), PropertyEditResult::Applied);
    EXPECT_EQ(ColliderOn(crate).Shape.HalfExtents, Vec3d(1.0f, 2.0f, 3.0f));

    ASSERT_EQ(Inspector->CommitEdit("collider:shape", "Sphere"), PropertyEditResult::Applied);
    EXPECT_EQ(ColliderOn(crate).Shape.Type, CollisionShapeType::Sphere);
    EXPECT_FLOAT_EQ(ColliderOn(crate).Shape.Radius, 3.0f) << "the sphere did not keep the box's room";
    rows = Inspector->Nodes();
    EXPECT_EQ(Find(rows, "collider:size"), nullptr);
    ASSERT_NE(Find(rows, "collider:radius"), nullptr);

    ASSERT_EQ(Inspector->CommitEdit("collider:sensor", "true"), PropertyEditResult::Applied);
    EXPECT_TRUE(ColliderOn(crate).IsTrigger);

    Workspace.Commands.Undo();
    Workspace.Commands.Undo();
    EXPECT_EQ(ColliderOn(crate).Shape.Type, CollisionShapeType::Box);
    EXPECT_FALSE(ColliderOn(crate).IsTrigger);
    EXPECT_EQ(ColliderOn(crate).Shape.HalfExtents, Vec3d(1.0f, 2.0f, 3.0f));
}

TEST_F(ColliderAuthoringTest, ACapsuleKeepsItsHeightWhileItsRadiusChanges)
{
    const EntityId crate = Crate();
    ASSERT_EQ(Inspector->Act("components", "add", "collider"), PropertyEditResult::Applied);
    ASSERT_EQ(Inspector->CommitEdit("collider:shape", "Capsule"), PropertyEditResult::Applied);
    ASSERT_EQ(Inspector->CommitEdit("collider:height", "3"), PropertyEditResult::Applied);
    ASSERT_EQ(Inspector->CommitEdit("collider:radius", "0.25"), PropertyEditResult::Applied);
    const CollisionShape& shape = ColliderOn(crate).Shape;
    EXPECT_FLOAT_EQ(shape.Radius, 0.25f);
    EXPECT_FLOAT_EQ(2.0f * (shape.HalfHeight + shape.Radius), 3.0f);
}

TEST_F(ColliderAuthoringTest, AScaledEntitySaysHowBigItsColliderIsInTheLevel)
{
    const EntityId crate = Crate(Vec3d(2.0f, 1.0f, 3.0f));
    ASSERT_EQ(Inspector->Act("components", "add", "collider"), PropertyEditResult::Applied);
    ASSERT_EQ(Inspector->CommitEdit("collider:size", "1, 1, 1"), PropertyEditResult::Applied);
    const std::vector<PropertyNode> rows = Inspector->Nodes();
    const PropertyNode* scaled = Find(rows, "collider:scaled");
    ASSERT_NE(scaled, nullptr);
    EXPECT_EQ(scaled->Value, "2, 1, 3 m across");
    (void)crate;
}

TEST_F(ColliderAuthoringTest, AColliderIsFittedToItsEntitysMeshWhenAddedAndOnRequest)
{
    const EntityId crate = Crate();
    const Aabb3d mesh = Aabb3d::FromMinMax(Vec3d(-1.0f, 0.0f, -0.5f), Vec3d(1.0f, 2.0f, 0.5f));
    const std::unique_ptr<IEditorComponentAdapter> adapter =
        MakeColliderEditorAdapter([mesh](const EditorDocument&, EntityId) { return std::optional<Aabb3d>(mesh); });

    Collider added;
    std::vector<std::byte> bytes(sizeof(Collider));
    std::memcpy(bytes.data(), &added, sizeof(Collider));
    adapter->PrepareAdded(Context(crate), bytes);
    std::memcpy(&added, bytes.data(), sizeof(Collider));
    EXPECT_EQ(added.Shape.Type, CollisionShapeType::Box);
    EXPECT_EQ(added.Shape.HalfExtents, Vec3d(1.0f, 1.0f, 0.5f));
    EXPECT_EQ(added.Shape.Center, Vec3d(0.0f, 1.0f, 0.0f)) << "the box was not around the mesh";

    // A collider sized by hand comes back to the mesh in one step.
    Entities().AddComponent<Collider>(crate, Collider{ CollisionShape::MakeSphere(0.2f) });
    EditorComponentInspectorContext context = Context(crate);
    const std::vector<PropertyNode> rows = adapter->InspectorNodes(context);
    ASSERT_FALSE(rows.empty());
    ASSERT_EQ(rows.front().Descriptor.Verbs.size(), 1u) << "a meshed entity offered no fit";
    ASSERT_EQ(adapter->ActInspector(context, "shape", "fit", ""), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(ColliderOn(crate).Shape.Radius, 1.0f);
    EXPECT_EQ(ColliderOn(crate).Shape.Center, Vec3d(0.0f, 1.0f, 0.0f));
    Workspace.Commands.Undo();
    EXPECT_FLOAT_EQ(ColliderOn(crate).Shape.Radius, 0.2f);
}

TEST_F(ColliderAuthoringTest, ASelectedBoxIsDrawnAndItsHandlesResizeItInTheEntitysFrame)
{
    const EntityId crate = Crate(Vec3d(2.0f, 1.0f, 1.0f));
    Entities().AddComponent<Collider>(crate, Collider{ CollisionShape::MakeBox(Vec3d(0.5f, 0.5f, 0.5f)) });

    ViewportAffordanceOutput output;
    Workspace.Affordances->Build(output);
    EXPECT_GE(output.Lines.size(), 12u) << "the box was not drawn";
    ASSERT_EQ(output.Aabbs.size(), 1u);
    EXPECT_EQ(output.Aabbs.front().LocalToWorld.Scale, Vec3d(2.0f, 1.0f, 1.0f));

    // The +X face dragged out by half a unit of the box's own frame.
    std::unique_ptr<IValueEditTransaction<Aabb3d>> edit = output.Aabbs.front().BeginEdit();
    edit->Commit(Aabb3d::FromMinMax(Vec3d(-0.5f, -0.5f, -0.5f), Vec3d(1.0f, 0.5f, 0.5f)));
    EXPECT_FLOAT_EQ(ColliderOn(crate).Shape.HalfExtents.X, 0.75f);
    EXPECT_FLOAT_EQ(ColliderOn(crate).Shape.Center.X, 0.25f) << "the opposite face moved";
    Workspace.Commands.Undo();
    EXPECT_FLOAT_EQ(ColliderOn(crate).Shape.HalfExtents.X, 0.5f);
    EXPECT_FLOAT_EQ(ColliderOn(crate).Shape.Center.X, 0.0f);
}
