#include <core/text/Slug.h>

#include <cctype>

std::string SlugOf(std::string_view text)
{
    std::string slug;
    slug.reserve(text.size());
    for (const unsigned char c : text)
    {
        if (std::isalnum(c) != 0)
            slug.push_back(static_cast<char>(std::tolower(c)));
        else if (!slug.empty() && slug.back() != '-')
            slug.push_back('-');
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    return slug;
}
