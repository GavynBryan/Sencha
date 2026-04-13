#include <scene/SceneNode2d.h>

#include <cassert>
#include <utility>

SceneNode2d::SceneNode2d(
    DataBatchHandle<Transform2f> localTransform,
    DataBatchHandle<Transform2f> worldTransform)
{
    Core.LocalTransform = std::move(localTransform);
    Core.WorldTransform = std::move(worldTransform);
}

const Transform2f& SceneNode2d::ResolveWorldTransform(
    const DataBatch<Transform2f>& worldTransforms) const
{
    const Transform2f* transform = worldTransforms.TryGet(Core.WorldTransform);
    assert(transform != nullptr && "SceneNode2d world transform handle is not valid.");
    return *transform;
}

const Transform2f& SceneNode2d::ResolveLocalTransform(
    const DataBatch<Transform2f>& localTransforms) const
{
    const Transform2f* transform = localTransforms.TryGet(Core.LocalTransform);
    assert(transform != nullptr && "SceneNode2d local transform handle is not valid.");
    return *transform;
}
