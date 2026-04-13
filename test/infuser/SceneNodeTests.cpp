#include <gtest/gtest.h>
#include <batch/DataBatch.h>
#include <math/Transform2.h>
#include <primitive/transform/core/TransformDefaults.h>
#include <primitive/transform/hierarchy/TransformHierarchyService.h>
#include <scene/SceneNode2d.h>
#include <service/ServiceHost.h>
#include <utility>

namespace
{
    using TransformBatch2D = DataBatch<Transform2f>;
    using LocalTransformTag = TransformDefaults::Tags::LocalTransformTag;
    using WorldTransformTag = TransformDefaults::Tags::WorldTransformTag;
    using TransformHierarchy2D =
        TransformHierarchyService<TransformDefaults::Tags::Transform2DTag>;

    struct SceneNode2dFixture
    {
        ServiceHost Host;
        TransformBatch2D& Locals;
        TransformBatch2D& Worlds;
        TransformHierarchy2D& Hierarchy;

        SceneNode2dFixture()
            : Locals(Host.AddTaggedService<TransformBatch2D, LocalTransformTag>())
            , Worlds(Host.AddTaggedService<TransformBatch2D, WorldTransformTag>())
            , Hierarchy(Host.AddService<TransformHierarchy2D>())
        {
        }
    };
}

TEST(SceneNode2d, OwnsLocalAndWorldTransformHandles)
{
    SceneNode2dFixture services;
    Transform2f localTransform({ 12.0f, 24.0f }, 0.5f, { 2.0f, 3.0f });
    auto localHandle = services.Locals.Emplace(localTransform);
    auto worldHandle = services.Worlds.Emplace(Transform2f::Identity());
    DataBatchKey transformKey = localHandle.GetToken();

    SceneNode2d node(std::move(localHandle), std::move(worldHandle));

    ASSERT_TRUE(services.Locals.Contains(transformKey));
    ASSERT_TRUE(services.Worlds.Contains(transformKey));
    EXPECT_FALSE(services.Hierarchy.IsRegistered(transformKey));

    const Transform2f* local = services.Locals.TryGet(node.Core.LocalTransform);
    const Transform2f* world = services.Worlds.TryGet(node.Core.WorldTransform);

    ASSERT_NE(local, nullptr);
    ASSERT_NE(world, nullptr);
    EXPECT_TRUE(local->NearlyEquals(localTransform));
    EXPECT_TRUE(world->NearlyEquals(Transform2f::Identity()));
}

TEST(SceneNode2d, ReleasesTransformsWithoutTouchingHierarchy)
{
    SceneNode2dFixture services;
    DataBatchKey transformKey;

    {
        auto localHandle = services.Locals.Emplace(Transform2f::Identity());
        auto worldHandle = services.Worlds.Emplace(Transform2f::Identity());
        transformKey = localHandle.GetToken();

        SceneNode2d node(std::move(localHandle), std::move(worldHandle));
        services.Hierarchy.Register(transformKey);

        ASSERT_TRUE(services.Locals.Contains(transformKey));
        ASSERT_TRUE(services.Worlds.Contains(transformKey));
        ASSERT_TRUE(services.Hierarchy.IsRegistered(transformKey));
    }

    EXPECT_FALSE(services.Locals.Contains(transformKey));
    EXPECT_FALSE(services.Worlds.Contains(transformKey));
    EXPECT_TRUE(services.Hierarchy.IsRegistered(transformKey));
}
