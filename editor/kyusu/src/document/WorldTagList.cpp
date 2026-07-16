#include "WorldTagList.h"

#include <utility>

std::vector<std::string> SplitWorldTagList(std::string_view text)
{
    std::vector<std::string> tags;
    std::string current;
    for (const char c : text)
    {
        if (c == ',' || c == ' ')
        {
            if (!current.empty())
                tags.push_back(std::move(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty())
        tags.push_back(std::move(current));
    return tags;
}
