#pragma once

#include "properties/PropertyNode.h"
#include "properties/PropertySource.h"

#include <cstddef>
#include <string_view>

struct AnimBoundRig;
struct AnimationNavigation;

// The rig's layers as a choice, `layer` chosen; how a lens's card says which
// layer it is about.
[[nodiscard]] PropertyNode AnimationLayerChoice(const AnimBoundRig& rig, std::size_t layer);
// The layer `value` names becomes the one navigated to, with no rule picked on it.
PropertyEditResult CommitAnimationLayerChoice(const AnimBoundRig& rig, AnimationNavigation& navigation,
                                              std::string_view value);
