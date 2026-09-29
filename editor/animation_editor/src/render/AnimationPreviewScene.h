#pragma once

#include <render/RenderQueue.h>
#include <render/SkinnedPoseFrameData.h>

#include <memory>

// Owner-thread extraction output. Render features never read the preview
// session or authoring documents while recording commands.
struct AnimationPreviewScene
{
    RenderQueue Queue;
    std::shared_ptr<SkinnedPoseFrameData> Poses = std::make_shared<SkinnedPoseFrameData>();
    Aabb3d Bounds = Aabb3d::Empty();
};
