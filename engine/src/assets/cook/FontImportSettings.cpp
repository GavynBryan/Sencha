#include <assets/cook/FontImportSettings.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <algorithm>
#include <cctype>
#include <format>

namespace
{
bool Fail(std::string* error, std::string message)
{
    if (error != nullptr)
        *error = std::move(message);
    return false;
}

bool EqualsNoCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x))
                   == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string_view FileStem(std::string_view sourceRelPath)
{
    const std::size_t slash = sourceRelPath.find_last_of('/');
    std::string_view name = slash == std::string_view::npos
        ? sourceRelPath
        : sourceRelPath.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

// The suffix conventions font vendors actually ship under, matched against the
// part of the stem after the last '-' or '_'. "Inter-SemiBold" and
// "JetBrainsMono_BoldItalic" both land.
struct WeightSuffix
{
    std::string_view Name;
    std::uint16_t Weight;
};

constexpr WeightSuffix kWeightSuffixes[] = {
    { "Thin", 100 },       { "ExtraLight", 200 }, { "UltraLight", 200 },
    { "Light", 300 },      { "Regular", 400 },    { "Normal", 400 },
    { "Book", 400 },       { "Medium", 500 },     { "SemiBold", 600 },
    { "DemiBold", 600 },   { "Bold", 700 },       { "ExtraBold", 800 },
    { "UltraBold", 800 },  { "Black", 900 },      { "Heavy", 900 },
};

constexpr std::string_view kItalicSuffix = "Italic";
} // namespace

std::string_view FontStyleName(FontStyle style)
{
    return style == FontStyle::Italic ? "italic" : "normal";
}

bool FontStyleFromName(std::string_view name, FontStyle& out)
{
    if (EqualsNoCase(name, "normal")) { out = FontStyle::Normal; return true; }
    if (EqualsNoCase(name, "italic")) { out = FontStyle::Italic; return true; }
    return false;
}

void InferFontFaceFromFileName(std::string_view sourceRelPath,
                               std::string& outFamily,
                               std::uint16_t& outWeight,
                               FontStyle& outStyle)
{
    const std::string_view stem = FileStem(sourceRelPath);
    outFamily.assign(stem);
    outWeight = kFontWeightNormal;
    outStyle = FontStyle::Normal;

    const std::size_t separator = stem.find_last_of("-_");
    if (separator == std::string_view::npos || separator + 1 >= stem.size())
        return;

    std::string_view suffix = stem.substr(separator + 1);
    FontStyle style = FontStyle::Normal;
    if (suffix.size() > kItalicSuffix.size()
        && EqualsNoCase(suffix.substr(suffix.size() - kItalicSuffix.size()), kItalicSuffix))
    {
        style = FontStyle::Italic;
        suffix = suffix.substr(0, suffix.size() - kItalicSuffix.size());
    }
    else if (EqualsNoCase(suffix, kItalicSuffix))
    {
        outFamily.assign(stem.substr(0, separator));
        outStyle = FontStyle::Italic;
        return;
    }

    for (const WeightSuffix& candidate : kWeightSuffixes)
    {
        if (!EqualsNoCase(suffix, candidate.Name))
            continue;
        outFamily.assign(stem.substr(0, separator));
        outWeight = candidate.Weight;
        outStyle = style;
        return;
    }

    // Unrecognised suffix: the whole stem stays the family, and any italic
    // marker we stripped above is put back by leaving outStyle alone.
}

bool ParseFontImportSettings(std::span<const std::byte> bytes,
                             FontImportSettings& out,
                             std::string* error)
{
    out = {};
    if (bytes.empty())
        return true;

    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    JsonParseError parseError;
    std::optional<JsonValue> root = JsonParse(text, &parseError);
    if (!root)
        return Fail(error, std::format("font settings: {}", parseError.Message));
    if (!root->IsObject())
        return Fail(error, "font settings: root must be an object");

    if (const JsonValue* family = root->Find("family"); family != nullptr)
    {
        if (!family->IsString() || family->AsString().empty())
            return Fail(error, "font settings: 'family' must be a non-empty string");
        out.Family = family->AsString();
    }

    if (const JsonValue* weight = root->Find("weight"); weight != nullptr)
    {
        if (!weight->IsNumber())
            return Fail(error, "font settings: 'weight' must be a number");
        const double value = weight->AsNumber();
        // The CSS scale. Out of range is a typo worth stopping for, not a value
        // to clamp: a face registered at the wrong weight silently never matches.
        if (value < 1.0 || value > 1000.0)
            return Fail(error, std::format("font settings: 'weight' {} is outside 1-1000", value));
        out.Weight = static_cast<std::uint16_t>(value);
    }

    if (const JsonValue* style = root->Find("style"); style != nullptr)
    {
        FontStyle parsed = FontStyle::Normal;
        if (!style->IsString() || !FontStyleFromName(style->AsString(), parsed))
            return Fail(error, "font settings: 'style' must be \"normal\" or \"italic\"");
        out.Style = parsed;
    }

    if (const JsonValue* fallback = root->Find("fallback"); fallback != nullptr)
    {
        if (!fallback->IsBool())
            return Fail(error, "font settings: 'fallback' must be a boolean");
        out.Fallback = fallback->AsBool();
    }

    return true;
}
