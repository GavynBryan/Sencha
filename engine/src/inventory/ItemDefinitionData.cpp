#include <inventory/ItemDefinitionData.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/json/JsonValue.h>
#include <ecs/World.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace
{
    DataFieldSchema Optional(DataFieldKind kind, std::string key, std::string label, std::string summary = {})
    {
        DataFieldSchema field = MakeDataField(kind, std::move(key), std::move(label), std::move(summary));
        field.Required = false;
        return field;
    }

    DataFieldSchema List(std::string key, std::string label, DataFieldSchema element)
    {
        DataFieldSchema list = Optional(DataFieldKind::Array, std::move(key), std::move(label));
        list.Editor.Widget = "cards";
        list.Children.push_back(std::move(element));
        return list;
    }

    DataFieldSchema TagList(std::string key, std::string label)
    {
        return List(std::move(key), std::move(label), MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag"));
    }

    DataFieldSchema UseSchema()
    {
        DataFieldSchema use = AbilityGrantEntriesSchema("use", "Used").Children.front();
        use.Key = "use";
        use.DisplayName = "Used";
        use.Summary = "What using it from a menu runs, with the entry as its source.";
        use.Required = false;
        std::erase_if(use.Children, [](const DataFieldSchema& child) { return child.Key == "key"; });
        return use;
    }

    DataSchema MakeSchema()
    {
        DataFieldSchema stack = Optional(DataFieldKind::Record, "stack", "Stack");
        DataFieldSchema max = Optional(DataFieldKind::Int, "max", "Most in one entry",
                                       "One means every unit is its own entry, with its own state.");
        max.Default = std::int64_t{ 1 };
        max.Numeric.Minimum = 1.0;
        stack.Children.push_back(std::move(max));

        DataFieldSchema keep = Optional(DataFieldKind::Bool, "keep", "Kept",
                                        "Off: taking it applies its take effects and keeps nothing.");
        keep.Default = true;

        DataFieldSchema effect = Optional(DataFieldKind::Record, "effect", "Effect");
        DataFieldSchema effectRef = MakeDataField(DataFieldKind::DataAssetRef, "data", "Effect");
        effectRef.Reference.DataSubtype = "effect.definition";
        effect.Children.push_back(std::move(effectRef));
        DataFieldSchema takeEffect = Optional(DataFieldKind::Record, "take_effect", "Effect");
        takeEffect.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"), std::move(effect) };
        DataFieldSchema take = Optional(DataFieldKind::Record, "take", "When taken");
        take.Children.push_back(List("effects", "Effects", std::move(takeEffect)));

        DataFieldSchema attribute = Optional(DataFieldKind::Record, "attribute", "Attribute");
        DataFieldSchema base = Optional(DataFieldKind::Float, "base", "Starts at");
        base.Default = 0.0;
        attribute.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"),
                               MakeDataField(DataFieldKind::String, "attribute", "Attribute"), std::move(base) };

        DataSchema schema;
        schema.TypeName = std::string(kItemDefinitionType);
        schema.DisplayName = "Item";
        schema.Description = "A kind of thing an actor can hold: how it stacks, what taking and using it do.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = { Optional(DataFieldKind::String, "name", "Name"),
                                 TagList("tags", "Tags"),
                                 TagList("instance_tags", "Each entry's own tags"),
                                 std::move(stack),
                                 std::move(keep),
                                 std::move(take),
                                 List("attributes", "Its own attributes", std::move(attribute)),
                                 UseSchema() };
        return schema;
    }

    std::string DataPath(const JsonValue* reference)
    {
        const JsonValue* data = reference != nullptr && reference->IsObject() ? reference->Find("data") : nullptr;
        return data != nullptr && data->IsString() ? data->AsString() : std::string{};
    }

    bool ReadTags(const JsonValue& data, std::string_view key, std::vector<std::string>& out)
    {
        const JsonValue* tags = data.Find(key);
        if (tags == nullptr || !tags->IsArray())
            return true;
        for (const JsonValue& tag : tags->AsArray())
        {
            if (!tag.IsString() || tag.AsString().empty())
                return false;
            out.push_back(tag.AsString());
        }
        return true;
    }

    DataAssetCompileResult Refused(std::string error) { return { {}, {}, std::move(error) }; }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        auto value = std::make_shared<ItemDeclaration>();
        std::vector<AssetRef> dependencies;
        if (const JsonValue* name = data.Find("name"); name != nullptr && name->IsString())
            value->Name = name->AsString();
        if (!ReadTags(data, "tags", value->Tags))
            return Refused("$.data.tags A tag is a name.");
        if (!ReadTags(data, "instance_tags", value->InstanceTags))
            return Refused("$.data.instance_tags A tag is a name.");
        if (const JsonValue* stack = data.Find("stack"); stack != nullptr && stack->IsObject())
            if (const JsonValue* max = stack->Find("max"); max != nullptr && max->IsNumber())
            {
                if (max->AsNumber() < 1.0)
                    return Refused("$.data.stack.max An entry holds at least one.");
                value->StackMax = static_cast<std::uint32_t>(std::min(max->AsNumber(), 4294967295.0));
            }
        if (const JsonValue* keep = data.Find("keep"); keep != nullptr && keep->IsBool())
            value->Keep = keep->AsBool();
        if (const JsonValue* take = data.Find("take"); take != nullptr && take->IsObject())
            if (const JsonValue* effects = take->Find("effects"); effects != nullptr && effects->IsArray())
                for (std::size_t i = 0; i < effects->AsArray().size(); ++i)
                {
                    const std::string path = DataPath(effects->AsArray()[i].Find("effect"));
                    if (path.empty())
                        return Refused(std::format("$.data.take.effects[{}].effect An effect is {{ \"data\": ... }}.", i));
                    dependencies.push_back(AssetRef{ .Type = AssetType::Data, .Path = path });
                    value->TakeEffects.push_back(path);
                }
        if (const JsonValue* attributes = data.Find("attributes"); attributes != nullptr && attributes->IsArray())
            for (std::size_t i = 0; i < attributes->AsArray().size(); ++i)
            {
                const JsonValue& entry = attributes->AsArray()[i];
                const JsonValue* attribute = entry.Find("attribute");
                if (attribute == nullptr || !attribute->IsString() || attribute->AsString().empty())
                    return Refused(std::format("$.data.attributes[{}].attribute An attribute is named.", i));
                const JsonValue* base = entry.Find("base");
                value->Attributes.push_back(
                    { attribute->AsString(), base != nullptr && base->IsNumber() ? static_cast<float>(base->AsNumber()) : 0.0f });
            }
        if (const JsonValue* use = data.Find("use"); use != nullptr && use->IsObject())
        {
            JsonValue::Object fields = use->AsObject();
            std::erase_if(fields, [](const auto& field) { return field.first == "key"; });
            fields.emplace_back("key", JsonValue(std::string("use")));
            std::vector<AbilityGrantDeclaration> declared;
            std::vector<AbilityDiagnostic> problems;
            if (!ParseAbilityGrantEntries(JsonValue(JsonValue::Array{ JsonValue(std::move(fields)) }), "$.data.use",
                                          declared, dependencies, problems)
                || declared.size() != 1)
                return Refused(problems.empty() ? std::string("$.data.use Use names an ability.")
                                                : std::format("{} {}", problems.front().Path, problems.front().Message));
            value->Use = std::move(declared.front());
        }
        if (!value->Keep && value->Use)
            return Refused("$.data.use An item that is not kept is never there to use.");
        return { std::move(value), std::move(dependencies), {} };
    }

    void ResolveTags(const GameplayTagRegistry* tags, const std::vector<std::string>& names,
                     std::vector<GameplayTagId>& out, std::vector<std::string>& errors)
    {
        for (const std::string& name : names)
        {
            const GameplayTagId tag = tags != nullptr ? tags->FindTag(name) : GameplayTagId{};
            if (tag.IsValid())
                out.push_back(tag);
            else
                errors.push_back(std::format("no gameplay tag '{}'", name));
        }
    }
}

void RegisterItemDefinitionData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kItemDefinitionType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kItemDefinitionType);
}

bool ResolveItemDeclaration(const ItemDeclaration& declaration, World& world, ItemDefinition& out,
                            std::vector<std::string>& errors)
{
    const std::size_t before = errors.size();
    out = ItemDefinition{};
    out.Name = declaration.Name;
    out.StackMax = declaration.StackMax;
    out.Keep = declaration.Keep;

    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    ResolveTags(tags, declaration.Tags, out.Tags, errors);
    ResolveTags(tags, declaration.InstanceTags, out.InstanceTags, errors);

    const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
    for (const std::string& path : declaration.TakeEffects)
    {
        const EffectId effect = effects != nullptr ? effects->Find(path) : EffectId{};
        const EffectDefinition* definition = effects != nullptr ? effects->Get(effect) : nullptr;
        if (definition == nullptr)
            errors.push_back(std::format("no effect '{}'", path));
        else if (definition->Duration != EffectDuration::Instant)
            errors.push_back(std::format("'{}' is not Instant, so taking an item cannot apply it once", path));
        else
            out.TakeEffects.push_back(effect);
    }

    for (const auto& [state, key] : { std::pair{ !declaration.InstanceTags.empty(), "instance_tags" },
                                      std::pair{ !declaration.Attributes.empty(), "attributes" } })
        if (state && declaration.StackMax > 1)
            errors.push_back(std::format("'{}' are each entry's own state, so 'stack.max' must be 1: splitting or "
                                         "merging a stack would mix them",
                                         key));
    if (declaration.InstanceTags.size() > GameplayTagContainer::Capacity)
        errors.push_back(std::format("'instance_tags' names {}, and an entry holds at most {} tags",
                                     declaration.InstanceTags.size(), GameplayTagContainer::Capacity));
    if (declaration.Attributes.size() > AttributeSet::Capacity)
        errors.push_back(std::format("'attributes' names {}, and an entry holds at most {} attributes",
                                     declaration.Attributes.size(), AttributeSet::Capacity));
    const AttributeRegistry* attributes = world.TryGetResource<AttributeRegistry>();
    for (const ItemDeclaration::Attribute& seed : declaration.Attributes)
    {
        const AttributeId attribute = attributes != nullptr ? attributes->FindAttribute(seed.Name) : AttributeId{};
        if (attribute.IsValid())
            out.Attributes.push_back({ attribute, seed.Base });
        else
            errors.push_back(std::format("no attribute '{}'", seed.Name));
    }

    if (declaration.Use)
    {
        AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
        AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
        const AbilityId ability = abilities != nullptr ? abilities->Reserve(declaration.Use->Ability) : AbilityId{};
        if (abilities == nullptr || entries == nullptr)
            errors.push_back("this World runs no abilities, so nothing can use it");
        else if (!ability.IsValid())
            errors.push_back(std::format("use names ability '{}', which the ability registry refused",
                                         declaration.Use->Ability));
        else
        {
            out.UseEntry = entries->Intern({ declaration.Use->Key, ability, declaration.Use->Override,
                                             declaration.Use->Parameters });
            if (out.UseEntry == kAbilityNoGrantEntry)
                errors.push_back(std::format("use grant '{}' {}", declaration.Use->Key, kAbilityGrantEntryRefusal));
        }
    }
    return errors.size() == before;
}
