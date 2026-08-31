// Hierarchy editing semantics: reparenting as an undoable command, subtree
// delete and duplicate, and the orphan rule when a lone parent dies. The
// transform composition itself is covered by SceneHierarchyTests; these cover
// the operations built on it.

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/DeleteEntityCommand.h"
#include "document/commands/DuplicateEntitiesCommand.h"
#include "document/commands/ReparentEntitiesCommand.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <span>
#include <vector>

namespace
{
    constexpr float kEpsilon = 1.0e-4f;

    void ExpectNear(const Vec3d& actual, const Vec3d& expected)
    {
        EXPECT_NEAR(actual.X, expected.X, kEpsilon);
        EXPECT_NEAR(actual.Y, expected.Y, kEpsilon);
        EXPECT_NEAR(actual.Z, expected.Z, kEpsilon);
    }

    class HierarchyCommandTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        Vec3d WorldPositionOf(EntityId entity)
        {
            Scene.RefreshDerivedTransforms();
            const Transform3f* world = Scene.TryGetWorldTransform(entity);
            return world != nullptr ? world->Position : Vec3d{};
        }

        LoggingProvider  Logging;
        EditorDocument   Document{ Logging };
        EditorScene&     Scene = Document.GetScene();
        SelectionContext Context;
        SelectionService Selection{ Context };
    };

    TEST_F(HierarchyCommandTest, SetParentRefusesSelfAndCycles)
    {
        const EntityId a = Scene.CreateEntity(Vec3d{});
        const EntityId b = Scene.CreateEntity(Vec3d{});
        const EntityId c = Scene.CreateEntity(Vec3d{});
        ASSERT_TRUE(Scene.SetParent(b, a));
        ASSERT_TRUE(Scene.SetParent(c, b));

        EXPECT_FALSE(Scene.SetParent(a, a));
        EXPECT_FALSE(Scene.SetParent(a, c)); // a is c's grandparent: cycle
        EXPECT_EQ(Scene.GetParent(a), EntityId{});
        EXPECT_TRUE(Scene.IsAncestorOf(a, c));
        EXPECT_FALSE(Scene.IsAncestorOf(c, a));
    }

    TEST_F(HierarchyCommandTest, ReparentKeepWorldDoesNotMoveTheChild)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 10.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{ 3.0f, 0.0f, 0.0f });
        Scene.RefreshDerivedTransforms();

        const std::array children{ child };
        auto command = MakeReparentEntitiesCommand(children, parent,
            ReparentTransformRule::KeepWorld, Scene, Document);
        ASSERT_NE(command, nullptr);
        command->Execute();

        EXPECT_EQ(Scene.GetParent(child), parent);
        ExpectNear(WorldPositionOf(child), Vec3d{ 3.0f, 0.0f, 0.0f });
        // The stored local is the re-expressed value, not the old one.
        ExpectNear(Scene.TryGetLocalTransform(child)->Position, Vec3d{ -7.0f, 0.0f, 0.0f });
    }

    TEST_F(HierarchyCommandTest, ReparentKeepLocalSnapsIntoTheParentFrame)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 10.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{ 3.0f, 0.0f, 0.0f });
        Scene.RefreshDerivedTransforms();

        const std::array children{ child };
        auto command = MakeReparentEntitiesCommand(children, parent,
            ReparentTransformRule::KeepLocal, Scene, Document);
        ASSERT_NE(command, nullptr);
        command->Execute();

        ExpectNear(WorldPositionOf(child), Vec3d{ 13.0f, 0.0f, 0.0f });
    }

    TEST_F(HierarchyCommandTest, ReparentUndoRestoresExactPriorState)
    {
        const EntityId oldParent = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        const EntityId newParent = Scene.CreateEntity(Vec3d{ 2.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{ 5.0f, 6.0f, 7.0f });
        ASSERT_TRUE(Scene.SetParent(child, oldParent));
        Scene.RefreshDerivedTransforms();
        const Transform3f localBefore = *Scene.TryGetLocalTransform(child);

        const std::array children{ child };
        auto command = MakeReparentEntitiesCommand(children, newParent,
            ReparentTransformRule::KeepWorld, Scene, Document);
        ASSERT_NE(command, nullptr);
        command->Execute();
        ASSERT_EQ(Scene.GetParent(child), newParent);

        command->Undo();
        EXPECT_EQ(Scene.GetParent(child), oldParent);
        // Bit-identical, not merely near: undo restores the captured value
        // rather than converting back.
        EXPECT_TRUE(*Scene.TryGetLocalTransform(child) == localBefore);
    }

    TEST_F(HierarchyCommandTest, ReparentFactoryRefusesADropOntoADescendant)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{});
        const EntityId child = Scene.CreateEntity(Vec3d{});
        ASSERT_TRUE(Scene.SetParent(child, parent));

        const std::array dragged{ parent };
        EXPECT_EQ(MakeReparentEntitiesCommand(dragged, child,
                      ReparentTransformRule::KeepWorld, Scene, Document),
                  nullptr);
    }

    TEST_F(HierarchyCommandTest, ReparentFactoryLetsANestedSelectionRideAlong)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{});
        const EntityId child = Scene.CreateEntity(Vec3d{});
        const EntityId destination = Scene.CreateEntity(Vec3d{});
        ASSERT_TRUE(Scene.SetParent(child, parent));

        // Both selected, dropped on destination: the branch moves, the child
        // stays inside it rather than being flattened beside its parent.
        const std::array dragged{ parent, child };
        auto command = MakeReparentEntitiesCommand(dragged, destination,
            ReparentTransformRule::KeepWorld, Scene, Document);
        ASSERT_NE(command, nullptr);
        command->Execute();

        EXPECT_EQ(Scene.GetParent(parent), destination);
        EXPECT_EQ(Scene.GetParent(child), parent);
    }

    TEST_F(HierarchyCommandTest, ABetweenRowsDropReordersWithoutTouchingTransforms)
    {
        const EntityId a = Scene.CreateEntity(Vec3d{ 1.0, 0.0, 0.0 });
        const EntityId b = Scene.CreateEntity(Vec3d{ 2.0, 0.0, 0.0 });
        const EntityId c = Scene.CreateEntity(Vec3d{ 3.0, 0.0, 0.0 });
        const Transform3f before = *Scene.TryGetLocalTransform(c);

        // c lands before a: a pure reorder among the same (root) siblings.
        const EntityId targets[] = { c };
        auto command = MakeReparentEntitiesCommand(
            targets, EntityId{}, ReparentTransformRule::KeepWorld,
            Scene, Document, /*reorder*/ true, /*insertBefore*/ a);
        ASSERT_NE(command, nullptr);
        command->Execute();

        const std::span<const EntityId> order = Scene.GetAllEntities();
        ASSERT_EQ(order.size(), 3u);
        EXPECT_EQ(order[0], c);
        EXPECT_EQ(order[1], a);
        EXPECT_EQ(order[2], b);
        // Same parent means the transform path never ran: bit-identical local.
        EXPECT_EQ(std::memcmp(Scene.TryGetLocalTransform(c), &before,
                              sizeof(Transform3f)),
                  0);

        command->Undo();
        const std::span<const EntityId> restored = Scene.GetAllEntities();
        EXPECT_EQ(restored[0], a);
        EXPECT_EQ(restored[1], b);
        EXPECT_EQ(restored[2], c);
    }

    TEST_F(HierarchyCommandTest, ABetweenRowsDropReparentsAndOrdersInOneStep)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 5.0, 0.0, 0.0 });
        const EntityId first = Scene.CreateEntity(Vec3d{});
        const EntityId second = Scene.CreateEntity(Vec3d{});
        const EntityId mover = Scene.CreateEntity(Vec3d{ 8.0, 0.0, 0.0 });
        ASSERT_TRUE(Scene.SetParent(first, parent));
        ASSERT_TRUE(Scene.SetParent(second, parent));

        const EntityId targets[] = { mover };
        auto command = MakeReparentEntitiesCommand(
            targets, parent, ReparentTransformRule::KeepWorld,
            Scene, Document, /*reorder*/ true, /*insertBefore*/ second);
        ASSERT_NE(command, nullptr);
        command->Execute();

        EXPECT_EQ(Scene.GetParent(mover), parent);
        ExpectNear(WorldPositionOf(mover), Vec3d{ 8.0, 0.0, 0.0 });
        // Sibling order is relative tracked order: first, mover, second.
        std::vector<EntityId> siblings;
        for (EntityId entity : Scene.GetAllEntities())
            if (Scene.GetParent(entity) == parent)
                siblings.push_back(entity);
        ASSERT_EQ(siblings.size(), 3u);
        EXPECT_EQ(siblings[0], first);
        EXPECT_EQ(siblings[1], mover);
        EXPECT_EQ(siblings[2], second);

        command->Undo();
        EXPECT_FALSE(Scene.GetParent(mover).IsValid());
        const std::span<const EntityId> restored = Scene.GetAllEntities();
        EXPECT_EQ(restored[3], mover);
    }

    TEST_F(HierarchyCommandTest, SetEntityOrderRefusesAnythingButAPermutation)
    {
        const EntityId a = Scene.CreateEntity(Vec3d{});
        const EntityId b = Scene.CreateEntity(Vec3d{});

        const EntityId shortList[] = { a };
        EXPECT_FALSE(Scene.SetEntityOrder(shortList));
        const EntityId duplicated[] = { a, a };
        EXPECT_FALSE(Scene.SetEntityOrder(duplicated));
        const EntityId swapped[] = { b, a };
        EXPECT_TRUE(Scene.SetEntityOrder(swapped));
        EXPECT_EQ(Scene.GetAllEntities()[0], b);
    }

    TEST_F(HierarchyCommandTest, SiblingOrderSurvivesASaveAndReload)
    {
        const EntityId a = Scene.CreateEntity(Vec3d{ 1.0, 0.0, 0.0 });
        const EntityId b = Scene.CreateEntity(Vec3d{ 2.0, 0.0, 0.0 });
        const auto pidOf = [](const EditorDocument& document, EntityId entity)
        {
            const auto* id = document.GetScene().GetRegistry()
                                 .Components.TryGet<PersistentIdComponent>(entity);
            return id != nullptr ? id->Id.Value : 0u;
        };
        const std::uint64_t pidA = pidOf(Document, a);
        const std::uint64_t pidB = pidOf(Document, b);

        const EntityId swapped[] = { b, a };
        ASSERT_TRUE(Scene.SetEntityOrder(swapped));

        EditorDocument reloaded(Logging);
        ASSERT_TRUE(reloaded.LoadFromSceneText(Document.ToSceneText()));
        const std::span<const EntityId> order =
            reloaded.GetScene().GetAllEntities();
        ASSERT_EQ(order.size(), 2u);
        EXPECT_EQ(pidOf(reloaded, order[0]), pidB);
        EXPECT_EQ(pidOf(reloaded, order[1]), pidA);
    }

    TEST_F(HierarchyCommandTest, DeleteRemovesTheBranchAndUndoRebuildsIt)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 10.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{});
        ASSERT_TRUE(Scene.SetParent(child, parent));
        Scene.SetWorldTransform(child, Transform3f{ Vec3d{ 12.0f, 0.0f, 0.0f },
                                                    Quat<float>::Identity(),
                                                    Vec3d::One() });
        Scene.RefreshDerivedTransforms();

        const std::array roots{ parent };
        auto command = MakeDeleteEntitiesCommand(roots, Scene, Document, Selection);
        command->Execute();
        EXPECT_EQ(Scene.GetEntityCount(), 0u);

        command->Undo();
        ASSERT_EQ(Scene.GetEntityCount(), 2u);
        // Handles are fresh; find the pair by parentage.
        EntityId restoredChild{};
        for (EntityId entity : Scene.GetAllEntities())
            if (Scene.GetParent(entity).IsValid())
                restoredChild = entity;
        ASSERT_TRUE(restoredChild.IsValid());
        EXPECT_EQ(Scene.GetParent(restoredChild),
                  Scene.GetParent(restoredChild)); // parent resolved and alive
        EXPECT_TRUE(Scene.HasEntity(Scene.GetParent(restoredChild)));
        ExpectNear(WorldPositionOf(restoredChild), Vec3d{ 12.0f, 0.0f, 0.0f });
    }

    TEST_F(HierarchyCommandTest, DestroyingALoneParentHandsChildrenToTheGrandparent)
    {
        const EntityId grandparent = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        const EntityId parent = Scene.CreateEntity(Vec3d{ 2.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{});
        ASSERT_TRUE(Scene.SetParent(parent, grandparent));
        ASSERT_TRUE(Scene.SetParent(child, parent));
        Scene.SetWorldTransform(child, Transform3f{ Vec3d{ 9.0f, 0.0f, 0.0f },
                                                    Quat<float>::Identity(),
                                                    Vec3d::One() });
        Scene.RefreshDerivedTransforms();

        Scene.DestroyEntity(parent);

        EXPECT_EQ(Scene.GetParent(child), grandparent);
        ExpectNear(WorldPositionOf(child), Vec3d{ 9.0f, 0.0f, 0.0f });
    }

    TEST_F(HierarchyCommandTest, DuplicateCopiesTheBranchAndRebindsItsParents)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{ 0.0f, 1.0f, 0.0f });
        ASSERT_TRUE(Scene.SetParent(child, parent));
        Scene.RefreshDerivedTransforms();

        const std::array sources{ parent };
        Transform3f target = Transform3f::Identity();
        target.Position = Vec3d{ 5.0f, 0.0f, 0.0f };
        const std::array targets{ target };
        DuplicateEntitiesCommand command(sources, targets, Scene, Document, Selection);
        command.Execute();

        ASSERT_EQ(Scene.GetEntityCount(), 4u);
        // The copied child is the one parented to something that is not the
        // original parent.
        EntityId copyChild{};
        for (EntityId entity : Scene.GetAllEntities())
        {
            const EntityId p = Scene.GetParent(entity);
            if (p.IsValid() && p != parent)
                copyChild = entity;
        }
        ASSERT_TRUE(copyChild.IsValid());
        const EntityId copyParent = Scene.GetParent(copyChild);
        EXPECT_NE(copyParent, parent);
        ExpectNear(WorldPositionOf(copyParent), Vec3d{ 5.0f, 0.0f, 0.0f });
        ExpectNear(WorldPositionOf(copyChild), Vec3d{ 5.0f, 1.0f, 0.0f });
        // Originals untouched.
        EXPECT_EQ(Scene.GetParent(child), parent);

        command.Undo();
        EXPECT_EQ(Scene.GetEntityCount(), 2u);
        EXPECT_TRUE(Scene.HasEntity(parent));
        EXPECT_TRUE(Scene.HasEntity(child));
    }

    TEST_F(HierarchyCommandTest, DuplicatingAChildAloneStaysUnderItsParent)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{ 0.0f, 1.0f, 0.0f });
        ASSERT_TRUE(Scene.SetParent(child, parent));
        Scene.RefreshDerivedTransforms();

        const std::array sources{ child };
        DuplicateEntitiesCommand command(sources, {}, Scene, Document, Selection);
        command.Execute();

        ASSERT_EQ(Scene.GetEntityCount(), 3u);
        std::size_t underParent = 0;
        for (EntityId entity : Scene.GetAllEntities())
            if (Scene.GetParent(entity) == parent)
                ++underParent;
        EXPECT_EQ(underParent, 2u); // original child and its copy, side by side
    }
} // namespace
