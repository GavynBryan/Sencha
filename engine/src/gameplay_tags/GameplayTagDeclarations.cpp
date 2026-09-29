#include <gameplay_tags/GameplayTagDeclarations.h>

#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <memory>

namespace
{
    DataSchema MakeSchema()
    {
        DataFieldSchema tag;
        tag.Kind = DataFieldKind::GameplayTag;
        tag.DisplayName = "Tag";
        tag.Summary = "A dotted gameplay tag name, e.g. Anim.Locomotion.Walk.";
        DataFieldSchema tags;
        tags.Key = "tags";
        tags.DisplayName = "Tags";
        tags.Kind = DataFieldKind::Array;
        tags.Children.push_back(std::move(tag));

        DataSchema schema;
        schema.TypeName = std::string(kGameplayTagDeclarationsType);
        schema.DisplayName = "Gameplay tag declarations";
        schema.Description = "Names the runtime registers when content loads, for content that needs names no "
                             "game module declares.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(std::move(tags));
        return schema;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        auto value = std::make_shared<GameplayTagDeclarations>();
        const JsonValue* tags = data.Find("tags");
        if (tags == nullptr || !tags->IsArray())
            return { {}, {}, "$.data.tags Declarations are a list of tag names." };
        GameplayTagRegistry syntax;
        for (std::size_t i = 0; i < tags->AsArray().size(); ++i)
        {
            const JsonValue& name = tags->AsArray()[i];
            GameplayTagError error;
            if (!name.IsString() || !syntax.RegisterTag(name.AsString(), &error))
                return { {}, {}, std::format("$.data.tags[{}] {}", i,
                                             name.IsString() ? error.Message : std::string("A tag is a name.")) };
            if (std::ranges::find(value->Tags, name.AsString()) != value->Tags.end())
                return { {}, {}, std::format("$.data.tags[{}] '{}' is declared twice.", i, name.AsString()) };
            value->Tags.push_back(name.AsString());
        }
        return { std::move(value), {}, {} };
    }
}

void RegisterGameplayTagDeclarations(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kGameplayTagDeclarationsType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kGameplayTagDeclarationsType);
}

void DeclareGameplayTags(const GameplayTagDeclarations& declarations, GameplayTagRegistry& tags,
                         std::vector<std::string>& errors)
{
    for (const std::string& name : declarations.Tags)
    {
        GameplayTagError error;
        if (!tags.RegisterTag(name, &error))
            errors.push_back(std::format("'{}': {}", name, error.Message));
    }
}
