#pragma once

#include <render/post/PostEffectCatalog.h>

#include <array>
#include <cstddef>
#include <vector>

inline constexpr std::size_t kPostEffectParamCount = 16;

struct PostEffectFrameEntry
{
    PostEffectId Effect;
    PostEffectDomain Domain = PostEffectDomain::Display;
    float Weight = 0.0f;
    std::array<float, kPostEffectParamCount> Params{};
};

// What the chain runs this frame for one camera, in (domain, registered order).
struct PostEffectFrame
{
    std::vector<PostEffectFrameEntry> Entries;
    // Wall seconds that keep advancing while the simulation is paused.
    float WallSeconds = 0.0f;
};
