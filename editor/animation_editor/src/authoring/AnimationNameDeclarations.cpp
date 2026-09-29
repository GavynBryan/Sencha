#include "authoring/AnimationNameDeclarations.h"

#include <core/metadata/DataSchema.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <charconv>

AnimationFieldAt FindAnimationField(const JsonValue& root, const DataFieldSchema& data, std::string_view path)
{
    constexpr std::string_view kData = "$.data";
    if (!path.starts_with(kData))
        return {};
    AnimationFieldAt at{ root.Find("data"), &data };
    path.remove_prefix(kData.size());
    while (!path.empty() && at.Value != nullptr)
    {
        if (path.front() == '.')
        {
            path.remove_prefix(1);
            const std::size_t end = path.find_first_of(".[");
            const std::string_view key = path.substr(0, end);
            path.remove_prefix(end == std::string_view::npos ? path.size() : end);
            const auto child = std::ranges::find(at.Field->Children, key, &DataFieldSchema::Key);
            if (!at.Value->IsObject() || child == at.Field->Children.end())
                return {};
            at = { at.Value->Find(key), &*child };
        }
        else if (path.front() == '[')
        {
            const std::size_t close = path.find(']');
            std::size_t index = 0;
            if (close == std::string_view::npos
                || std::from_chars(path.data() + 1, path.data() + close, index).ec != std::errc{}
                || !at.Value->IsArray() || index >= at.Value->AsArray().size() || at.Field->Children.size() != 1)
                return {};
            path.remove_prefix(close + 1);
            at = { &at.Value->AsArray()[index], &at.Field->Children.front() };
        }
        else
        {
            return {};
        }
    }
    return at.Value != nullptr ? at : AnimationFieldAt{};
}

std::vector<std::string> UndeclaredAnimationNames(const std::vector<AnimDiagnostic>& diagnostics,
                                                  const GameplayTagRegistry& tags,
                                                  const std::function<AnimationDocumentView(std::string_view)>& document)
{
    std::vector<std::string> names;
    for (const AnimDiagnostic& diagnostic : diagnostics)
    {
        const AnimationDocumentView view = document(diagnostic.AssetPath);
        if (view.Root == nullptr || view.Data == nullptr)
            continue;
        const AnimationFieldAt at = FindAnimationField(*view.Root, *view.Data, diagnostic.FieldPath);
        if (at.Field == nullptr || at.Field->Kind != DataFieldKind::GameplayTag || !at.Value->IsString())
            continue;
        const std::string& name = at.Value->AsString();
        GameplayTagError error;
        GameplayTagRegistry syntax;
        if (tags.FindTag(name).IsValid() || !syntax.RegisterTag(name, &error)
            || std::ranges::find(names, name) != names.end())
            continue;
        names.push_back(name);
    }
    return names;
}

bool AddAnimationTagDeclarations(JsonValue& root, const std::vector<std::string>& names)
{
    JsonValue* data = root.Find("data");
    if (data == nullptr || !data->IsObject())
        return false;
    JsonValue* tags = data->Find("tags");
    if (tags == nullptr)
    {
        data->AsObject().emplace_back("tags", JsonValue(JsonValue::Array{}));
        tags = &data->AsObject().back().second;
    }
    if (!tags->IsArray())
        return false;
    bool changed = false;
    for (const std::string& name : names)
    {
        const bool listed = std::ranges::any_of(tags->AsArray(), [&](const JsonValue& tag) {
            return tag.IsString() && tag.AsString() == name;
        });
        if (!listed)
        {
            tags->AsArray().emplace_back(name);
            changed = true;
        }
    }
    return changed;
}
