#include <authored/AuthoredArgumentMatch.h>

#include <algorithm>

std::vector<AuthoredArgumentSlot> MatchAuthoredArguments(const DataFieldSchema& declared,
                                                         std::span<const std::string_view> given,
                                                         std::vector<std::string_view>& undeclared)
{
    for (const std::string_view name : given)
        if (std::ranges::none_of(declared.Children, [&](const DataFieldSchema& field) { return field.Key == name; }))
            undeclared.push_back(name);
    std::vector<AuthoredArgumentSlot> slots;
    slots.reserve(declared.Children.size());
    for (const DataFieldSchema& field : declared.Children)
    {
        AuthoredArgumentSlot slot{ .Field = &field, .Given = std::nullopt };
        if (const auto found = std::ranges::find(given, std::string_view(field.Key)); found != given.end())
            slot.Given = static_cast<std::size_t>(found - given.begin());
        slots.push_back(slot);
    }
    return slots;
}
