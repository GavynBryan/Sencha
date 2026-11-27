#pragma once

#include "properties/PropertyNode.h"

#include <string>
#include <string_view>

// One entity a reference may be set to: the value an edit takes (its target's
// persistent identity as text), what the target is called and where it is.
struct EntityReferenceChoice
{
    std::string Value;
    std::string Label;
    std::string Place;
};

namespace EntityReferenceVerbs
{
inline constexpr std::string_view Clear = "clear";
inline constexpr std::string_view Select = "select";
} // namespace EntityReferenceVerbs

// `value` empty reads as `none`. Whoever knows the documents says how a set one
// reads and offers its target for selection.
[[nodiscard]] PropertyNode EntityReferenceProperty(std::string key, std::string label, std::string value,
                                                   std::string none, std::string tooltip = {});
