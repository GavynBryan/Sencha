#pragma once

#include "properties/PropertyNode.h"

#include <cstddef>
#include <optional>
#include <string_view>

struct AnimBoundRig;

// The rig's layers as a choice, `layer` chosen; how a lens's card says which
// layer it is about.
[[nodiscard]] PropertyNode AnimationLayerChoice(const AnimBoundRig& rig, std::size_t layer);
// The layer `value` names, or nothing when it names none.
[[nodiscard]] std::optional<std::size_t> ParseAnimationLayerChoice(const AnimBoundRig& rig, std::string_view value);
