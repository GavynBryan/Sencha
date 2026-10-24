#pragma once

#include <core/metadata/DataSchema.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

struct AuthoredArgumentSlot
{
    const DataFieldSchema* Field = nullptr;
    // Where, among the authored arguments, the one naming this field is.
    std::optional<std::size_t> Given;
};

// The arguments `declared` lists, in its order, each paired with the authored
// argument named for it; an authored name it does not declare joins `undeclared`.
[[nodiscard]] std::vector<AuthoredArgumentSlot> MatchAuthoredArguments(const DataFieldSchema& declared,
                                                                       std::span<const std::string_view> given,
                                                                       std::vector<std::string_view>& undeclared);
