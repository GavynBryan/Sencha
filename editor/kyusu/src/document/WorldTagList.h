#pragma once

#include <string>
#include <string_view>
#include <vector>

[[nodiscard]] std::vector<std::string> SplitWorldTagList(std::string_view text);
