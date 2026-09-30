#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string_view>

// Case-insensitive substring match for filter boxes. An empty needle matches
// everything, which is what an empty filter field means.
[[nodiscard]] inline bool TextFilterMatch(std::string_view haystack,
                                          std::string_view needle)
{
    if (needle.empty())
        return true;
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b)
        {
            return std::tolower(static_cast<unsigned char>(a))
                == std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

// How well `needle` names `haystack`, best first: 0 where the text begins with
// it, 1 where a word inside it does, 2 anywhere else; nullopt for no match.
// Case-insensitive, and an empty needle ranks everything 0. For lists ordered
// by relevance, where a filter box only asks whether something matches.
[[nodiscard]] inline std::optional<int> TextFilterRank(std::string_view haystack,
                                                       std::string_view needle)
{
    if (needle.empty())
        return 0;
    const auto equal = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    std::optional<int> best;
    auto from = haystack.begin();
    while (true)
    {
        const auto it = std::search(from, haystack.end(), needle.begin(), needle.end(), equal);
        if (it == haystack.end())
            return best;
        const std::size_t at = static_cast<std::size_t>(it - haystack.begin());
        const bool wordStart = at == 0 || !std::isalnum(static_cast<unsigned char>(haystack[at - 1]));
        const int rank = at == 0 ? 0 : wordStart ? 1 : 2;
        if (!best.has_value() || rank < *best)
            best = rank;
        if (rank == 0)
            return best;
        from = it + 1;
    }
}
