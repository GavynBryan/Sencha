#include <anim/AnimRequestSchema.h>

#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <memory>

namespace
{
DataSchema MakeSchema()
{
    DataFieldSchema name;
    name.Key = "name";
    name.DisplayName = "Parameter name";
    name.Kind = DataFieldKind::String;
    name.Summary = "Name used by ReqParam(intent, name); consumes one 32-bit parameter slot.";

    DataFieldSchema kind;
    kind.Key = "kind";
    kind.DisplayName = "Value kind";
    kind.Kind = DataFieldKind::Enum;
    kind.Default = std::string("float");
    kind.EnumChoices = { {"float", "Float", "32-bit floating point"},
        {"int", "Integer", "Signed 32-bit integer"}, {"bool", "Boolean", "True or false"},
        {"tag", "Gameplay tag", "Authored by name; resolved per World"} };

    DataFieldSchema param;
    param.Kind = DataFieldKind::Record;
    param.DisplayName = "Parameter";
    param.Children = {std::move(name), std::move(kind)};
    DataFieldSchema params;
    params.Key = "params";
    params.Kind = DataFieldKind::Array;
    params.DisplayName = "Parameters (maximum four)";
    params.Editor.TitleKey = "name";
    params.Editor.Widget = "cards";
    params.Children.push_back(std::move(param));

    DataFieldSchema tag;
    tag.Key = "intent";
    tag.DisplayName = "Intent";
    tag.Kind = DataFieldKind::GameplayTag;
    tag.Summary = "Presentation intent, not a gameplay operation or target.";
    DataFieldSchema intent;
    intent.Kind = DataFieldKind::Record;
    intent.DisplayName = "Request intent";
    intent.Children = {std::move(tag), std::move(params)};
    DataFieldSchema intents;
    intents.Key = "intents";
    intents.DisplayName = "Request intents";
    intents.Kind = DataFieldKind::Array;
    intents.Editor.TitleKey = "intent";
    intents.Editor.Widget = "cards";
    intents.Children.push_back(std::move(intent));
    DataSchema schema;
    schema.TypeName = kAnimRequestSchemaType;
    schema.DisplayName = "Animation request schema";
    schema.Description = "Named, typed presentation request parameters; precedence belongs to selector rules.";
    schema.Root.Kind = DataFieldKind::Record;
    schema.Root.Children.push_back(std::move(intents));
    return schema;
}

DataAssetCompileResult Compile(const JsonValue& data)
{
    auto value = std::make_shared<AnimRequestSchema>();
    const auto& intents = data.Find("intents")->AsArray();
    GameplayTagRegistry tagSyntax;
    constexpr std::array<std::string_view, 4> kinds{"float", "int", "bool", "tag"};
    for (std::size_t i = 0; i < intents.size(); ++i)
    {
        const auto path = std::format("$.data.intents[{}]", i);
        AnimRequestIntentDeclaration intent;
        intent.Intent = intents[i].Find("intent")->AsString();
        GameplayTagError error;
        if (!tagSyntax.RegisterTag(intent.Intent, &error))
            return {{}, {}, path + ".intent " + error.Message};
        if (std::any_of(value->Intents.begin(), value->Intents.end(), [&](const auto& previous) {
            return previous.Intent == intent.Intent;
        }))
            return {{}, {}, path + ".intent Duplicate intent; edit its existing declaration."};
        const auto& params = intents[i].Find("params")->AsArray();
        if (params.size() > intent.Params.size())
            return {{}, {}, path + ".params At most four parameters; publish additional gameplay state as facts."};
        for (std::size_t p = 0; p < params.size(); ++p)
        {
            const auto paramPath = std::format("{}.params[{}]", path, p);
            auto& param = intent.Params[p];
            param.Name = params[p].Find("name")->AsString();
            if (param.Name.empty() || param.Name.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
                return {{}, {}, paramPath + ".name Use a nonempty name containing letters, digits or underscores."};
            for (std::size_t earlier = 0; earlier < p; ++earlier)
                if (intent.Params[earlier].Name == param.Name)
                    return {{}, {}, paramPath + ".name Duplicate parameter name within this intent."};
            const auto& kind = params[p].Find("kind")->AsString();
            const auto found = std::find(kinds.begin(), kinds.end(), kind);
            // The registered schema validates the closed enum before compilation.
            param.Kind = static_cast<AnimRequestParamKind>(found - kinds.begin());
        }
        intent.ParamCount = static_cast<std::uint8_t>(params.size());
        value->Intents.push_back(std::move(intent));
    }
    return {std::move(value), {}, {}};
}
}

void RegisterAnimRequestSchema(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({std::string(kAnimRequestSchemaType), 1, Compile})) return;
    if (!schemas.Register(MakeSchema())) (void)types.Unregister(kAnimRequestSchemaType);
}
