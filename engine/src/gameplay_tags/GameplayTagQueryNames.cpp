#include <gameplay_tags/GameplayTagQueryNames.h>

#include <gameplay_tags/GameplayTagRegistry.h>

GameplayTagQuery ResolveTagQuery(const GameplayTagQueryNames& names, const GameplayTagRegistry& tags,
                                 std::vector<std::string>& missing)
{
    GameplayTagQuery query;
    const auto add = [&](const std::vector<std::string>& listed, auto addClause) {
        for (const std::string& name : listed)
        {
            const GameplayTagId tag = tags.FindTag(name);
            if (!tag.IsValid())
            {
                missing.push_back(name);
                continue;
            }
            (query.*addClause)(tag, GameplayTagMatchMode::Hierarchical);
        }
    };
    add(names.All, &GameplayTagQuery::AddAll);
    add(names.Any, &GameplayTagQuery::AddAny);
    add(names.None, &GameplayTagQuery::AddNone);
    return query;
}

DataFieldSchema MakeTagQueryNamesField(std::string key, std::string displayName)
{
    const auto tagList = [](std::string listKey, std::string listName) {
        DataFieldSchema list = MakeDataField(DataFieldKind::Array, std::move(listKey), std::move(listName));
        list.Required = false;
        list.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag"));
        return list;
    };
    DataFieldSchema query = MakeDataField(DataFieldKind::Record, std::move(key), std::move(displayName));
    query.Required = false;
    query.Children.push_back(tagList("all", "All of"));
    query.Children.push_back(tagList("any", "Any of"));
    query.Children.push_back(tagList("none", "None of"));
    return query;
}

GameplayTagQueryNames ReadTagQueryNames(const JsonValue& query)
{
    GameplayTagQueryNames names;
    const auto read = [&query](std::string_view listKey, std::vector<std::string>& out) {
        if (const JsonValue* list = query.Find(listKey))
        {
            for (const JsonValue& tag : list->AsArray())
                out.push_back(tag.AsString());
        }
    };
    read("all", names.All);
    read("any", names.Any);
    read("none", names.None);
    return names;
}
