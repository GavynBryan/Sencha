#include <assets/cook/UiSourceScan.h>

#include <algorithm>
#include <cctype>

namespace
{
bool IsSpace(char c)
{
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

// Replaces comment bodies with spaces rather than deleting them, so every byte
// offset -- and therefore every reported line number -- still lines up with the
// file a human will open.
std::string StripComments(std::string_view text, UiBlobKind kind)
{
    std::string out(text);
    const std::string_view open = kind == UiBlobKind::Document ? "<!--" : "/*";
    const std::string_view close = kind == UiBlobKind::Document ? "-->" : "*/";

    std::size_t cursor = 0;
    while ((cursor = out.find(open, cursor)) != std::string::npos)
    {
        std::size_t end = out.find(close, cursor + open.size());
        end = end == std::string::npos ? out.size() : end + close.size();
        for (std::size_t i = cursor; i < end; ++i)
        {
            if (out[i] != '\n')
                out[i] = ' ';
        }
        cursor = end;
    }
    return out;
}

std::uint32_t LineAt(std::string_view text, std::size_t offset)
{
    return static_cast<std::uint32_t>(
        std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n') + 1);
}

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && IsSpace(value.front()))
        value.remove_prefix(1);
    while (!value.empty() && IsSpace(value.back()))
        value.remove_suffix(1);
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')
        && value.back() == value.front())
    {
        value.remove_prefix(1);
        value.remove_suffix(1);
    }
    return value;
}

// The value of `attribute="..."` in the tag beginning at `tagStart`, or empty.
std::string_view AttributeValue(std::string_view text, std::size_t tagStart,
                                std::string_view attribute)
{
    const std::size_t tagEnd = text.find('>', tagStart);
    const std::string_view tag = text.substr(tagStart,
        (tagEnd == std::string_view::npos ? text.size() : tagEnd) - tagStart);

    std::size_t cursor = 0;
    while ((cursor = tag.find(attribute, cursor)) != std::string_view::npos)
    {
        std::size_t equals = tag.find('=', cursor + attribute.size());
        if (equals == std::string_view::npos)
            return {};
        // Only whitespace may sit between the name and the '=', or this is a
        // different attribute whose name happens to contain ours.
        const std::string_view between = tag.substr(cursor + attribute.size(),
                                                    equals - cursor - attribute.size());
        if (!std::all_of(between.begin(), between.end(), IsSpace))
        {
            cursor += attribute.size();
            continue;
        }

        std::size_t valueStart = equals + 1;
        while (valueStart < tag.size() && IsSpace(tag[valueStart]))
            ++valueStart;
        if (valueStart >= tag.size())
            return {};

        const char quote = tag[valueStart];
        if (quote != '"' && quote != '\'')
            return {};
        const std::size_t valueEnd = tag.find(quote, valueStart + 1);
        if (valueEnd == std::string_view::npos)
            return {};
        return tag.substr(valueStart + 1, valueEnd - valueStart - 1);
    }
    return {};
}

// The argument of a functional value -- image(...), url(...) -- starting at the
// opening parenthesis after `keyword`.
std::string_view FunctionArgument(std::string_view text, std::size_t keywordEnd)
{
    std::size_t open = keywordEnd;
    while (open < text.size() && IsSpace(text[open]))
        ++open;
    if (open >= text.size() || text[open] != '(')
        return {};
    const std::size_t close = text.find(')', open + 1);
    if (close == std::string_view::npos)
        return {};
    return Trim(text.substr(open + 1, close - open - 1));
}

void AddResource(UiSourceReferences& out, AssetType type, std::string_view path)
{
    if (path.empty())
        return;
    const std::string value(path);
    const bool already = std::any_of(out.Resources.begin(), out.Resources.end(),
        [&](const AssetRef& ref) { return ref.Type == type && ref.Path == value; });
    if (!already)
        out.Resources.push_back(AssetRef{ type, value });
}

void AddStyleSheet(UiSourceReferences& out, std::string_view path)
{
    if (path.empty())
        return;
    const std::string value(path);
    if (std::find(out.StyleSheets.begin(), out.StyleSheets.end(), value) == out.StyleSheets.end())
        out.StyleSheets.push_back(value);
}

// Constructs the supported profile does not cover. Each needs an optional
// render-interface method Sencha does not implement, so authoring one produces
// something that would silently not draw -- see docs/ui/architecture.md §7.
struct UnsupportedConstruct
{
    std::string_view Needle;
    std::string_view Feature;
};

constexpr UnsupportedConstruct kUnsupported[] = {
    { "box-shadow", "box-shadow" },
    { "backdrop-filter", "backdrop-filter" },
    { "mask-image", "mask-image" },
    { "linear-gradient", "gradient decorator" },
    { "radial-gradient", "gradient decorator" },
    { "conic-gradient", "gradient decorator" },
    { "horizontal-gradient", "gradient decorator" },
    { "vertical-gradient", "gradient decorator" },
};

void ScanUnsupported(std::string_view text, std::string_view sourcePath, UiSourceReferences& out)
{
    for (const UnsupportedConstruct& construct : kUnsupported)
    {
        std::size_t cursor = 0;
        while ((cursor = text.find(construct.Needle, cursor)) != std::string_view::npos)
        {
            out.Unsupported.push_back(UiUnsupportedFeature{
                std::string(construct.Feature), std::string(sourcePath), LineAt(text, cursor) });
            cursor += construct.Needle.size();
        }
    }

    // "filter" on its own, but not the "backdrop-filter" already reported above
    // and not a property that merely ends in it.
    std::size_t cursor = 0;
    while ((cursor = text.find("filter", cursor)) != std::string_view::npos)
    {
        const bool precededByNameChar = cursor > 0
            && (std::isalnum(static_cast<unsigned char>(text[cursor - 1])) != 0
                || text[cursor - 1] == '-' || text[cursor - 1] == '_');
        std::size_t after = cursor + 6;
        while (after < text.size() && IsSpace(text[after]))
            ++after;
        const bool isProperty = after < text.size() && text[after] == ':';
        if (!precededByNameChar && isProperty)
        {
            out.Unsupported.push_back(UiUnsupportedFeature{
                "filter", std::string(sourcePath), LineAt(text, cursor) });
        }
        cursor += 6;
    }
}
} // namespace

void ScanUiSource(std::string_view text,
                  UiBlobKind kind,
                  std::string_view sourcePath,
                  UiSourceReferences& out)
{
    const std::string stripped = StripComments(text, kind);
    const std::string_view body = stripped;

    if (kind == UiBlobKind::Document)
    {
        std::size_t cursor = 0;
        while ((cursor = body.find("<link", cursor)) != std::string_view::npos)
        {
            AddStyleSheet(out, Trim(AttributeValue(body, cursor, "href")));
            cursor += 5;
        }

        cursor = 0;
        while ((cursor = body.find("<img", cursor)) != std::string_view::npos)
        {
            AddResource(out, AssetType::Texture, Trim(AttributeValue(body, cursor, "src")));
            cursor += 4;
        }
    }

    // @import applies to inline <style> blocks as well as to stylesheets, so it
    // is scanned in both kinds.
    std::size_t cursor = 0;
    while ((cursor = body.find("@import", cursor)) != std::string_view::npos)
    {
        std::size_t valueStart = cursor + 7;
        while (valueStart < body.size() && IsSpace(body[valueStart]))
            ++valueStart;

        if (body.compare(valueStart, 3, "url") == 0)
        {
            AddStyleSheet(out, FunctionArgument(body, valueStart + 3));
        }
        else
        {
            const std::size_t end = body.find_first_of(";\n", valueStart);
            AddStyleSheet(out, Trim(body.substr(valueStart,
                (end == std::string_view::npos ? body.size() : end) - valueStart)));
        }
        cursor = valueStart;
    }

    // @font-face { src: <path>; }
    cursor = 0;
    while ((cursor = body.find("@font-face", cursor)) != std::string_view::npos)
    {
        const std::size_t blockEnd = body.find('}', cursor);
        const std::string_view block = body.substr(cursor,
            (blockEnd == std::string_view::npos ? body.size() : blockEnd) - cursor);
        const std::size_t src = block.find("src");
        if (src != std::string_view::npos)
        {
            const std::size_t colon = block.find(':', src + 3);
            if (colon != std::string_view::npos)
            {
                const std::size_t end = block.find_first_of(";\n", colon + 1);
                AddResource(out, AssetType::Font, Trim(block.substr(colon + 1,
                    (end == std::string_view::npos ? block.size() : end) - colon - 1)));
            }
        }
        cursor += 10;
    }

    // decorator: image( <path> )
    cursor = 0;
    while ((cursor = body.find("image", cursor)) != std::string_view::npos)
    {
        AddResource(out, AssetType::Texture, FunctionArgument(body, cursor + 5));
        cursor += 5;
    }

    ScanUnsupported(body, sourcePath, out);
}
