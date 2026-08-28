// The editor's derived transforms: an entity's WorldTransform is composed from
// its LocalTransform and its ancestors, and a world-space placement converts
// back through the parent. Flat scenes make the two indistinguishable, which is
// exactly why these need their own coverage -- every other editor test would
// still pass with the composition removed.

#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <core/logging/LoggingProvider.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <numbers>

namespace
{
    class SceneHierarchyTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterDocumentSerializers(); }

        // The editor has no reparenting API yet; the transform math it will run
        // on does. Writing the component directly keeps these tests about the
        // composition rather than about a command that does not exist.
        void SetParent(EntityId child, EntityId parent)
        {
            Scene.GetRegistry().Components.AddComponent<Parent>(child, Parent{ parent });
        }

        LoggingProvider Logging;
        EditorDocument  Document{ Logging };
        EditorScene&    Scene = Document.GetScene();
    };

    constexpr float kEpsilon = 1.0e-4f;

    void ExpectNear(const Vec3d& actual, const Vec3d& expected)
    {
        EXPECT_NEAR(actual.X, expected.X, kEpsilon);
        EXPECT_NEAR(actual.Y, expected.Y, kEpsilon);
        EXPECT_NEAR(actual.Z, expected.Z, kEpsilon);
    }

    TEST_F(SceneHierarchyTest, UnparentedEntityWorldTransformMatchesItsLocal)
    {
        const EntityId entity = Scene.CreateEntity(Vec3d{ 3.0f, 4.0f, 5.0f });
        Scene.RefreshDerivedTransforms();

        const Transform3f* world = Scene.TryGetWorldTransform(entity);
        ASSERT_NE(world, nullptr);
        ExpectNear(world->Position, Vec3d{ 3.0f, 4.0f, 5.0f });
    }

    TEST_F(SceneHierarchyTest, ChildComposesThroughItsParent)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 10.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        SetParent(child, parent);
        Scene.RefreshDerivedTransforms();

        const Transform3f* world = Scene.TryGetWorldTransform(child);
        ASSERT_NE(world, nullptr);
        ExpectNear(world->Position, Vec3d{ 11.0f, 0.0f, 0.0f });
    }

    TEST_F(SceneHierarchyTest, ChildComposesParentRotationAndScale)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 0.0f });
        Transform3f parentTransform = *Scene.TryGetLocalTransform(parent);
        // A quarter turn about +Y takes +X to -Z, and the doubled scale takes
        // the child's one unit of offset to two.
        parentTransform.Rotation = Quat<float>::FromAxisAngle(
            Vec3d{ 0.0f, 1.0f, 0.0f }, std::numbers::pi_v<float> / 2.0f);
        parentTransform.Scale = Vec3d{ 2.0f, 2.0f, 2.0f };
        Scene.SetTransform(parent, parentTransform);

        const EntityId child = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        SetParent(child, parent);
        Scene.RefreshDerivedTransforms();

        const Transform3f* world = Scene.TryGetWorldTransform(child);
        ASSERT_NE(world, nullptr);
        ExpectNear(world->Position, Vec3d{ 0.0f, 0.0f, -2.0f });
        ExpectNear(world->Scale, Vec3d{ 2.0f, 2.0f, 2.0f });
    }

    TEST_F(SceneHierarchyTest, EveryAncestorContributes)
    {
        const EntityId root = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        const EntityId middle = Scene.CreateEntity(Vec3d{ 0.0f, 2.0f, 0.0f });
        const EntityId leaf = Scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 3.0f });
        SetParent(middle, root);
        SetParent(leaf, middle);
        Scene.RefreshDerivedTransforms();

        const Transform3f* world = Scene.TryGetWorldTransform(leaf);
        ASSERT_NE(world, nullptr);
        ExpectNear(world->Position, Vec3d{ 1.0f, 2.0f, 3.0f });
    }

    TEST_F(SceneHierarchyTest, CompositionDoesNotDependOnCreationOrder)
    {
        // The child exists before the parent it will hang from, which is what a
        // load produces whenever the file lists them that way.
        const EntityId child = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        const EntityId parent = Scene.CreateEntity(Vec3d{ 10.0f, 0.0f, 0.0f });
        SetParent(child, parent);
        Scene.RefreshDerivedTransforms();

        const Transform3f* world = Scene.TryGetWorldTransform(child);
        ASSERT_NE(world, nullptr);
        ExpectNear(world->Position, Vec3d{ 11.0f, 0.0f, 0.0f });
    }

    TEST_F(SceneHierarchyTest, WorldPlacementOnAChildRoundTrips)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 0.0f });
        Transform3f parentTransform = *Scene.TryGetLocalTransform(parent);
        parentTransform.Position = Vec3d{ 5.0f, 0.0f, 0.0f };
        parentTransform.Rotation = Quat<float>::FromAxisAngle(
            Vec3d{ 0.0f, 1.0f, 0.0f }, std::numbers::pi_v<float> / 2.0f);
        parentTransform.Scale = Vec3d{ 2.0f, 2.0f, 2.0f };
        Scene.SetTransform(parent, parentTransform);

        const EntityId child = Scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 0.0f });
        SetParent(child, parent);
        Scene.RefreshDerivedTransforms();

        // What a gizmo does: hand the scene a world-space placement and expect
        // the entity to end up exactly there.
        Transform3f target = Transform3f::Identity();
        target.Position = Vec3d{ 7.0f, 3.0f, -1.0f };
        target.Scale = Vec3d{ 2.0f, 2.0f, 2.0f };
        Scene.SetWorldTransform(child, target);
        Scene.RefreshDerivedTransforms();

        const Transform3f* world = Scene.TryGetWorldTransform(child);
        ASSERT_NE(world, nullptr);
        ExpectNear(world->Position, target.Position);
        ExpectNear(world->Scale, target.Scale);

        // The stored value is the parent-relative one, not the world value that
        // was handed in -- otherwise moving the parent would not carry the child.
        const Transform3f* local = Scene.TryGetLocalTransform(child);
        ASSERT_NE(local, nullptr);
        EXPECT_GT((local->Position - target.Position).Magnitude(), kEpsilon);
    }

    TEST_F(SceneHierarchyTest, WorldPlacementWithoutAParentIsTheLocalValue)
    {
        const EntityId entity = Scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 0.0f });

        Transform3f target = Transform3f::Identity();
        target.Position = Vec3d{ 4.0f, 5.0f, 6.0f };
        Scene.SetWorldTransform(entity, target);

        const Transform3f* local = Scene.TryGetLocalTransform(entity);
        ASSERT_NE(local, nullptr);
        ExpectNear(local->Position, target.Position);
    }

    TEST_F(SceneHierarchyTest, MovingAParentCarriesTheChild)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 0.0f });
        const EntityId child = Scene.CreateEntity(Vec3d{ 1.0f, 0.0f, 0.0f });
        SetParent(child, parent);
        Scene.RefreshDerivedTransforms();

        Transform3f moved = *Scene.TryGetLocalTransform(parent);
        moved.Position = Vec3d{ 0.0f, 8.0f, 0.0f };
        Scene.SetTransform(parent, moved);
        Scene.RefreshDerivedTransforms();

        const Transform3f* world = Scene.TryGetWorldTransform(child);
        ASSERT_NE(world, nullptr);
        ExpectNear(world->Position, Vec3d{ 1.0f, 8.0f, 0.0f });
    }

    TEST_F(SceneHierarchyTest, WorldBoundsFollowTheParent)
    {
        const EntityId parent = Scene.CreateEntity(Vec3d{ 0.0f, 0.0f, 0.0f });
        const EntityId brush = Scene.CreateBrush(Vec3d{ 0.0f, 0.0f, 0.0f },
                                                 Vec3d{ 1.0f, 1.0f, 1.0f });
        SetParent(brush, parent);

        Transform3f moved = *Scene.TryGetLocalTransform(parent);
        moved.Position = Vec3d{ 20.0f, 0.0f, 0.0f };
        Scene.SetTransform(parent, moved);
        Scene.RefreshDerivedTransforms();

        // Picking, framing, and the render queue all read bounds; a parented
        // brush whose bounds ignored the parent would be clickable in one place
        // and drawn in another.
        const std::optional<Aabb3d> bounds = Scene.TryGetWorldBounds(brush);
        ASSERT_TRUE(bounds.has_value());
        ExpectNear(bounds->Center(), Vec3d{ 20.0f, 0.0f, 0.0f });
    }
} // namespace
