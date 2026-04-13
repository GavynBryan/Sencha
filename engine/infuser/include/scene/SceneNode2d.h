#pragma once

#include <vector>
#include <scene/SceneNodeCore.h>

//=============================================================================
// SceneNode2d
//
// Represents a node in a 2D scene graph. Each node has a unique ID,
// parent-child relationships, and associated local/world transform keys.
//=============================================================================
class SceneNode2d
{
public:
    SceneNode2d() = default;
    SceneNode2d(
        DataBatchHandle<Transform2f> localTransform,
        DataBatchHandle<Transform2f> worldTransform);
    ~SceneNode2d() = default;

    SceneNode2d(SceneNode2d&& other) noexcept = default;
    SceneNode2d& operator=(SceneNode2d&& other) noexcept = default;

    SceneNode2d(const SceneNode2d&) = delete;
    SceneNode2d& operator=(const SceneNode2d&) = delete;

    const Transform2f& ResolveWorldTransform(const DataBatch<Transform2f>& worldTransforms) const;
    const Transform2f& ResolveLocalTransform(const DataBatch<Transform2f>& localTransforms) const;

    SceneNodeCore Core;
};
