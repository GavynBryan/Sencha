#pragma once

#include <string>
#include <string_view>

// Lower-case letters and digits, every other run folded into one '-', with
// none at either end: "Loss Function" is "loss-function". Empty when `text`
// has no letter or digit.
[[nodiscard]] std::string SlugOf(std::string_view text);
