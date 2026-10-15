#pragma once

#include <core/json/JsonValue.h>

#include <span>
#include <string>
#include <string_view>

// A shape of ability to start from: an ordinary document an author edits like
// any other, its keys minted afresh each time it is made.
struct AbilityRecipe
{
    std::string_view Name;
    std::string_view Summary;
    JsonValue (*Make)();
};

[[nodiscard]] std::span<const AbilityRecipe> AbilityRecipes();
[[nodiscard]] const AbilityRecipe* FindAbilityRecipe(std::string_view name);
