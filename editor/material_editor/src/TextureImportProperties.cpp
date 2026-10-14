#include "TextureImportProperties.h"

#include "properties/SettingProperties.h"

#include <array>

namespace
{
constexpr std::array<std::string_view, 6> kUsages = { "From its name", "Base color", "Normal",
                                                      "ORM",           "Emissive",   "Linear data" };
constexpr std::array<std::string_view, 2> kFilters = { "Linear", "Nearest" };
} // namespace

std::vector<PropertyNode> TextureImportProperties::Nodes() const
{
    if (Current.Texture.empty())
        return {};
    PropertyNode import = GroupProperty("import", "Import");
    import.Descriptor.Verbs.push_back({ .Id = "create_material", .Label = "Create Material" });
    if (Current.Settings.has_value())
    {
        const TextureImportSettings& settings = *Current.Settings;
        import.Children.push_back(ChoiceProperty("usage", "Used as", kUsages, static_cast<std::size_t>(settings.Usage),
                                                 "Which colour space and cook it gets"));
        import.Children.push_back(ChoiceProperty("filter", "Filter", kFilters, static_cast<std::size_t>(settings.Filter),
                                                 "Nearest keeps pixel art crisp"));
        import.Children.push_back(ToggleProperty("compress", "Block compressed", settings.Compress,
                                                 "Off keeps every texel exact, at four times the memory"));
        import.Children.push_back(ToggleProperty("mips", "Mipmaps", settings.GenerateMips));
    }
    import.Children.push_back(NoteProperty("cooked", "Cooked", Current.Cooked));
    if (!Current.Problem.empty())
        import.Children.push_back(NoteProperty("problem", "Not applied", Current.Problem));
    return { std::move(import) };
}

bool TextureImportProperties::BeginEdit(std::string_view key)
{
    return Current.Settings.has_value() && (key == "usage" || key == "filter" || key == "compress" || key == "mips");
}

PropertyEditResult TextureImportProperties::CommitEdit(std::string_view key, std::string_view value)
{
    if (!Current.Settings.has_value())
        return PropertyEditResult::Gone;
    TextureImportSettings changed = *Current.Settings;
    bool parsed = false;
    if (key == "usage")
    {
        const std::optional<std::size_t> usage = ParseChoice(value, kUsages);
        if ((parsed = usage.has_value()))
            changed.Usage = static_cast<TextureUsage>(*usage);
    }
    else if (key == "filter")
    {
        const std::optional<std::size_t> filter = ParseChoice(value, kFilters);
        if ((parsed = filter.has_value()))
            changed.Filter = static_cast<TextureFilter>(*filter);
    }
    else if (key == "compress" || key == "mips")
    {
        const std::optional<bool> on = ParseToggle(value);
        if ((parsed = on.has_value()))
            (key == "compress" ? changed.Compress : changed.GenerateMips) = *on;
    }
    else
        return PropertyEditResult::Gone;
    if (!parsed)
        return PropertyEditResult::Refused;
    if (changed == *Current.Settings)
        return PropertyEditResult::Unchanged;
    std::string cooked;
    std::string error;
    if (!Apply || !Apply(Current.Texture, changed, cooked, error))
    {
        Current.Problem = error.empty() ? std::string("The settings could not be applied") : error;
        return PropertyEditResult::Refused;
    }
    Current.Settings = changed;
    Current.Cooked = std::move(cooked);
    Current.Problem.clear();
    return PropertyEditResult::Applied;
}

PropertyEditResult TextureImportProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    if (key != "import" || verb != "create_material" || Current.Texture.empty() || !CreateMaterial)
        return PropertyEditResult::Gone;
    CreateMaterial(Current.Texture);
    return PropertyEditResult::Applied;
}
