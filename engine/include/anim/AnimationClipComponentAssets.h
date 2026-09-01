#pragma once

class AnimationClipCache;

// The cache a clip player retains through, published as a world resource
// exactly like SkinnedMeshComponentAssets.
struct AnimationClipComponentAssets
{
    AnimationClipComponentAssets() = default;
    explicit AnimationClipComponentAssets(AnimationClipCache* clips)
        : Clips(clips)
    {
    }

    AnimationClipCache* Clips = nullptr;
};
