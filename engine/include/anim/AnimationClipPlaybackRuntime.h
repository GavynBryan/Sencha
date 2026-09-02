#pragma once

class AnimationClipCache;

//=============================================================================
// AnimationClipPlaybackRuntime
//
// World resource naming the cache AnimationClipPlaybackSystem reads clip
// durations from. A world without one holds every player's time where it is.
//=============================================================================
struct AnimationClipPlaybackRuntime
{
    AnimationClipPlaybackRuntime() = default;
    explicit AnimationClipPlaybackRuntime(AnimationClipCache* clips)
        : Clips(clips)
    {
    }

    AnimationClipCache* Clips = nullptr;
};
