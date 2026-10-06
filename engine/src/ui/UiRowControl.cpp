#include <ui/UiScreenDesc.h>

#include <array>
#include <utility>

namespace
{
constexpr std::array<std::pair<UiRowControl, std::string_view>, 11> kNames{ {
    { UiRowControl::Text, "text" },
    { UiRowControl::Range, "range" },
    { UiRowControl::Choice, "choice" },
    { UiRowControl::Toggle, "toggle" },
    { UiRowControl::Asset, "asset" },
    { UiRowControl::Color, "color" },
    { UiRowControl::Section, "section" },
    { UiRowControl::Action, "action" },
    { UiRowControl::LongText, "longtext" },
    { UiRowControl::Number, "number" },
    { UiRowControl::Vector, "vector" },
} };
} // namespace

std::string_view UiRowControlName(UiRowControl control)
{
    for (const auto& [value, name] : kNames)
        if (value == control)
            return name;
    return "text";
}

std::optional<UiRowControl> ParseUiRowControl(std::string_view name)
{
    for (const auto& [value, known] : kNames)
        if (known == name)
            return value;
    return std::nullopt;
}
