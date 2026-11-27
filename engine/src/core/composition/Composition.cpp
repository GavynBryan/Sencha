#include <core/composition/Composition.h>

#include <core/json/JsonValue.h>

#include <charconv>

namespace CompositionDetail
{
    std::string_view ParentPath(std::string_view path)
    {
        if (path.size() <= 1)
            return {};
        const char last = path.back();
        const char opener = last == '}' ? '{' : last == ']' ? '[' : '.';
        const std::size_t at = opener == '.' ? path.find_last_of(".[{") : path.rfind(opener);
        return at == std::string_view::npos ? std::string_view{} : path.substr(0, at);
    }

    bool IsUnder(std::string_view path, std::string_view ancestor)
    {
        if (!path.starts_with(ancestor))
            return false;
        if (path.size() == ancestor.size())
            return true;
        const char next = path[ancestor.size()];
        return next == '.' || next == '[' || next == '{';
    }
}

const CompositionOrigin* FindCompositionOrigin(const CompositionProvenance& provenance, std::string_view path)
{
    for (std::string_view at = path; !at.empty(); at = CompositionDetail::ParentPath(at))
        if (const auto found = provenance.find(std::string(at)); found != provenance.end())
            return &found->second;
    return nullptr;
}

std::string CompositionLogicalPath(const JsonValue& composed, const DataFieldSchema& root, std::string_view indexPath)
{
    if (!indexPath.starts_with("$"))
        return std::string(indexPath);
    std::string logical = "$";
    const JsonValue* value = &composed;
    const DataFieldSchema* node = UnwrapDataField(&root);
    std::size_t at = 1;
    while (at < indexPath.size())
    {
        if (indexPath[at] == '.')
        {
            const std::size_t end = indexPath.find_first_of(".[", at + 1);
            const std::string_view name = indexPath.substr(at + 1, end == std::string_view::npos ? end : end - at - 1);
            logical += ".";
            logical += name;
            value = value != nullptr ? value->Find(name) : nullptr;
            node = node != nullptr && node->Kind == DataFieldKind::Record
                ? UnwrapDataField(FindChild(*node, name))
                : nullptr;
            at = end == std::string_view::npos ? indexPath.size() : end;
            continue;
        }
        if (indexPath[at] != '[')
            return std::string(indexPath);
        const std::size_t close = indexPath.find(']', at);
        if (close == std::string_view::npos)
            return std::string(indexPath);
        std::size_t index = 0;
        const std::string_view digits = indexPath.substr(at + 1, close - at - 1);
        std::from_chars(digits.data(), digits.data() + digits.size(), index);
        const DataFieldSchema* key =
            node != nullptr && node->Kind == DataFieldKind::Array ? ArrayMergeKey(*node) : nullptr;
        const JsonValue* element =
            value != nullptr && value->IsArray() && index < value->Size() ? &value->AsArray()[index] : nullptr;
        const JsonValue* name = key != nullptr && element != nullptr ? element->Find(key->Key) : nullptr;
        if (name != nullptr && name->IsString())
            logical += "{" + name->AsString() + "}";
        else
            logical += std::string(indexPath.substr(at, close - at + 1));
        value = element;
        node = node != nullptr && node->Kind == DataFieldKind::Array && node->Children.size() == 1
            ? UnwrapDataField(&node->Children.front())
            : nullptr;
        at = close + 1;
    }
    return logical;
}
