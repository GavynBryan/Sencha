#pragma once

#include <cstdint>

struct AnimBoundRig;
class AnimationClipCache;
class GameplayTagRegistry;

// A hash of what machines must agree on for one request set to play out the same,
// built from names rather than this process's ids. See docs/gameplay/animation.md.
[[nodiscard]] std::uint64_t AnimRigTimingIdentity(const AnimBoundRig& rig, const AnimationClipCache* clips,
                                                  const GameplayTagRegistry* tags);
