#include <inventory/ItemDefinitionData.h>

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/assets/AssetRefCodec.h>
#include <core/json/JsonValue.h>
#include <ecs/World.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/Inventory.h>

#include <algorithm>
#include <format>
#include <memory>
#include <utility>

namespace
{
    DataFieldSchema List(std::string key, std::string label, DataFieldSchema element)
    {
        DataFieldSchema list = MakeOptionalDataField(DataFieldKind::Array, std::move(key), std::move(label));
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
        DataFieldSchema stack = MakeOptionalDataField(DataFieldKind::Record, "stack", "Stack");
        DataFieldSchema max = MakeOptionalDataField(DataFieldKind::Int, "max", "Most in one entry",
                                       "One means every unit is its own entry, with its own state.");
        max.Default = std::int64_t{ 1 };
        max.Numeric.Minimum = 1.0;
        stack.Children.push_back(std::move(max));

        DataFieldSchema keep = MakeOptionalDataField(DataFieldKind::Bool, "keep", "Kept",
                                        "Off: taking it applies its take effects and keeps nothing.");
        keep.Default = true;

        DataFieldSchema effect = MakeOptionalDataField(DataFieldKind::Record, "effect", "Effect");
        DataFieldSchema effectRef = MakeDataField(DataFieldKind::DataAssetRef, "data", "Effect");
        effectRef.Reference.DataSubtype = "effect.definition";
        effect.Children.push_back(std::move(effectRef));
        DataFieldSchema takeEffect = MakeOptionalDataField(DataFieldKind::Record, "take_effect", "Effect");
        takeEffect.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"), std::move(effect) };
        DataFieldSchema take = MakeOptionalDataField(DataFieldKind::Record, "take", "When taken");
        take.Children.push_back(List("effects", "Effects", std::move(takeEffect)));

        DataFieldSchema attribute = MakeOptionalDataField(DataFieldKind::Record, "attribute", "Attribute");
        DataFieldSchema base = MakeOptionalDataField(DataFieldKind::Float, "base", "Starts at");
        base.Default = 0.0;
        attribute.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"),
                               MakeDataField(DataFieldKind::String, "attribute", "Attribute"), std::move(base) };

        DataFieldSchema equipEffect = MakeOptionalDataField(DataFieldKind::Record, "equip_effect", "Effect");
        DataFieldSchema equipEffectRef = MakeOptionalDataField(DataFieldKind::Record, "effect", "Effect");
        DataFieldSchema equipEffectData = MakeDataField(DataFieldKind::DataAssetRef, "data", "Effect");
        equipEffectData.Reference.DataSubtype = "effect.definition";
        equipEffectRef.Children.push_back(std::move(equipEffectData));
        DataFieldSchema scale = MakeOptionalDataField(DataFieldKind::Float, "scale", "Scale");
        scale.Default = 1.0;
        equipEffect.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"), std::move(equipEffectRef),
                                 std::move(scale),
                                 MakeOptionalDataField(DataFieldKind::GameplayTag, "scale_by", "Scaled by",
                                                       "A number property of this item the scale is multiplied by.") };
        DataFieldSchema equip = MakeOptionalDataField(DataFieldKind::Record, "equip", "When equipped",
                                                      "The slots it takes and what it maintains in them.");
        DataFieldSchema require = MakeOptionalDataField(DataFieldKind::Record, "require", "Requires of its wearer",
                                                        "Checked when it is equipped, never while it is worn.");
        require.Children.push_back(MakeTagQueryNamesField("tags", "Wearer's tags"));
        equip.Children = { List("occupies", "Occupies", MakeDataField(DataFieldKind::String, "slot", "Slot or group")),
                           List("effects", "Effects", std::move(equipEffect)),
                           TagList("lock_tags", "Locked while it holds"),
                           std::move(require) };

        DataFieldSchema grant = MakeOptionalDataField(DataFieldKind::Record, "grant", "Grant");
        grant.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"),
                           AbilityGrantParametersSchema("parameters", "Parameters") };

        DataFieldSchema value = MakeDataField(DataFieldKind::Record, "value", "Value", "Exactly one of these.");
        value.Editor.Widget = "compact";
        DataFieldSchema text = MakeOptionalDataField(DataFieldKind::String, "text", "Text");
        text.Editor.Multiline = true;
        value.Children = { MakeOptionalDataField(DataFieldKind::Float, "number", "Number"), std::move(text),
                           MakeOptionalDataField(DataFieldKind::GameplayTag, "tag", "Tag"),
                           MakeOptionalDataField(DataFieldKind::AssetRef, "asset", "Asset"),
                           MakeOptionalDataField(DataFieldKind::DataAssetRef, "data", "Data") };
        DataFieldSchema property = MakeOptionalDataField(DataFieldKind::Record, "property", "Property");
        property.Children = { MakeDataField(DataFieldKind::StableKey, "key", "Key"),
                              MakeDataField(DataFieldKind::GameplayTag, "property", "Property",
                                            "The game's name for the fact."),
                              std::move(value) };

        DataSchema schema;
        schema.TypeName = std::string(kItemDefinitionType);
        schema.DisplayName = "Item";
        schema.Description = "A kind of thing an actor can hold: how it stacks, what taking and using it do.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = { MakeOptionalDataField(DataFieldKind::String, "name", "Name"),
                                 TagList("tags", "Tags"),
                                 TagList("instance_tags", "Each entry's own tags"),
                                 std::move(stack),
                                 std::move(keep),
                                 std::move(take),
                                 List("attributes", "Its own attributes", std::move(attribute)),
                                 UseSchema(),
                                 std::move(equip),
                                 List("grants", "Its grants' parameters", std::move(grant)),
                                 List("properties", "Properties", std::move(property)) };
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

    bool ReadProperty(const JsonValue& entry, const std::string& at, ItemDeclaration::Property& out,
                      std::vector<AssetRef>& dependencies, std::string& error)
    {
        const JsonValue* name = entry.Find("property");
        if (name == nullptr || !name->IsString() || name->AsString().empty())
        {
            error = at + ".property A property is named by a tag.";
            return false;
        }
        out.Name = name->AsString();
        const JsonValue* value = entry.Find("value");
        const auto member = [value](std::string_view key) {
            return value != nullptr && value->IsObject() ? value->Find(key) : nullptr;
        };
        const JsonValue* number = member("number");
        const JsonValue* text = member("text");
        const JsonValue* tag = member("tag");
        const JsonValue* asset = member("asset");
        const JsonValue* data = member("data");
        if ((number != nullptr) + (text != nullptr) + (tag != nullptr) + (asset != nullptr) + (data != nullptr) != 1)
        {
            error = at + ".value A value holds exactly one of number, text, tag, asset or data.";
            return false;
        }
        std::optional<AssetRef> reference;
        if (asset != nullptr || data != nullptr)
            reference = AssetRefFromJson(asset != nullptr ? *asset : *data);
        if (number != nullptr && number->IsNumber())
            out.Value = AuthoredValue::Float(number->AsNumber());
        else if (text != nullptr && text->IsString())
            out.Value = AuthoredValue::String(text->AsString());
        else if (tag != nullptr && tag->IsString() && !tag->AsString().empty())
            out.Tag = tag->AsString();
        else if (reference.has_value())
        {
            if (data != nullptr)
                reference->Type = AssetType::Data;
            dependencies.push_back(*reference);
            out.Value = data != nullptr ? AuthoredValue::DataAsset(*reference) : AuthoredValue::Asset(*reference);
        }
        else
        {
            error = at + ".value The value is not of the kind its member names.";
            return false;
        }
        return true;
    }

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
        if (const JsonValue* equip = data.Find("equip"); equip != nullptr && equip->IsObject())
        {
            if (!ReadTags(*equip, "occupies", value->Occupies))
                return Refused("$.data.equip.occupies A slot or group is named.");
            if (!ReadTags(*equip, "lock_tags", value->EquipLockTags))
                return Refused("$.data.equip.lock_tags A tag is a name.");
            if (const JsonValue* require = equip->Find("require"); require != nullptr && require->IsObject())
                if (const JsonValue* tags = require->Find("tags"); tags != nullptr && tags->IsObject())
                    value->EquipRequire = ReadTagQueryNames(*tags);
            if (const JsonValue* effects = equip->Find("effects"); effects != nullptr && effects->IsArray())
                for (std::size_t i = 0; i < effects->AsArray().size(); ++i)
                {
                    const JsonValue& entry = effects->AsArray()[i];
                    const std::string path = DataPath(entry.Find("effect"));
                    if (path.empty())
                        return Refused(std::format("$.data.equip.effects[{}].effect An effect is {{ \"data\": ... }}.", i));
                    const JsonValue* scale = entry.Find("scale");
                    const JsonValue* scaleBy = entry.Find("scale_by");
                    dependencies.push_back(AssetRef{ .Type = AssetType::Data, .Path = path });
                    value->EquipEffects.push_back(
                        { path, scale != nullptr && scale->IsNumber() ? static_cast<float>(scale->AsNumber()) : 1.0f,
                          scaleBy != nullptr && scaleBy->IsString() ? scaleBy->AsString() : std::string{} });
                }
            if (value->Occupies.empty())
                return Refused("$.data.equip.occupies Equipping takes at least one slot.");
            if (!value->Keep)
                return Refused("$.data.equip An item that is not kept is never there to equip.");
        }
        if (const JsonValue* grants = data.Find("grants"); grants != nullptr && grants->IsArray())
            for (std::size_t i = 0; i < grants->AsArray().size(); ++i)
            {
                const JsonValue& entry = grants->AsArray()[i];
                const std::string at = std::format("$.data.grants[{}]", i);
                const JsonValue* key = entry.Find("key");
                if (key == nullptr || !key->IsString() || key->AsString().empty())
                    return Refused(at + ".key A grant is named by its key.");
                ItemGrantParameters parameters{ .Key = key->AsString(), .Parameters = {} };
                std::vector<AbilityDiagnostic> problems;
                if (const JsonValue* list = entry.Find("parameters");
                    list != nullptr && !ParseAbilityGrantParameters(*list, at + ".parameters", parameters.Parameters, problems))
                    return Refused(std::format("{} {}", problems.front().Path, problems.front().Message));
                value->GrantParameters.push_back(std::move(parameters));
            }
        if (const JsonValue* properties = data.Find("properties"); properties != nullptr && properties->IsArray())
            for (std::size_t i = 0; i < properties->AsArray().size(); ++i)
            {
                ItemDeclaration::Property property;
                std::string error;
                if (!ReadProperty(properties->AsArray()[i], std::format("$.data.properties[{}]", i), property,
                                  dependencies, error))
                    return Refused(std::move(error));
                value->Properties.push_back(std::move(property));
            }
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

    bool EquipEffectsGrant(const World& world, const std::vector<ItemEquipEffect>& equipped, std::string_view key)
    {
        const AbilityEffectGrants* grants = world.TryGetResource<AbilityEffectGrants>();
        const AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
        return grants != nullptr && entries != nullptr
            && std::ranges::any_of(equipped, [&](const ItemEquipEffect& effect) {
                   return std::ranges::any_of(grants->Of(effect.Effect), [&](std::uint16_t index) {
                       const AbilityGrantEntry* entry = entries->Find(index);
                       return entry != nullptr && entry->Key == key;
                   });
               });
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
    ResolveTags(tags, declaration.EquipLockTags, out.EquipLockTags, errors);
    if (!declaration.EquipRequire.Empty())
    {
        std::vector<std::string> missing;
        if (tags == nullptr)
            errors.push_back("equip.require: this World has no gameplay tags");
        else
            out.EquipRequire = ResolveTagQuery(declaration.EquipRequire, *tags, missing);
        for (const std::string& name : missing)
            errors.push_back(std::format("equip.require: no gameplay tag '{}'", name));
        out.HasEquipRequire = true;
    }

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
    for (const ItemDeclaration::Property& property : declaration.Properties)
    {
        const GameplayTagId name = tags != nullptr ? tags->FindTag(property.Name) : GameplayTagId{};
        const GameplayTagId tag = tags != nullptr && !property.Tag.empty() ? tags->FindTag(property.Tag) : GameplayTagId{};
        if (!name.IsValid())
            errors.push_back(std::format("properties: no gameplay tag '{}'", property.Name));
        else if (ItemPropertyOf(out, name) != nullptr)
            errors.push_back(std::format("properties: '{}' is stated twice", property.Name));
        else if (!property.Tag.empty() && !tag.IsValid())
            errors.push_back(std::format("properties: '{}' holds no gameplay tag '{}'", property.Name, property.Tag));
        else
            out.Properties.push_back({ name, property.Tag.empty() ? property.Value : AuthoredValue::Tag(tag) });
    }

    out.Occupies = declaration.Occupies;
    out.GrantParameters = declaration.GrantParameters;
    for (const ItemDeclaration::EquipEffect& equipped : declaration.EquipEffects)
    {
        const EffectId effect = effects != nullptr ? effects->Find(equipped.Effect) : EffectId{};
        const EffectDefinition* definition = effects != nullptr ? effects->Get(effect) : nullptr;
        const GameplayTagId scaleBy =
            tags != nullptr && !equipped.ScaleBy.empty() ? tags->FindTag(equipped.ScaleBy) : GameplayTagId{};
        const std::optional<double> by = equipped.ScaleBy.empty() ? std::optional<double>(1.0) : ItemNumberOf(out, scaleBy);
        if (definition == nullptr)
            errors.push_back(std::format("no effect '{}'", equipped.Effect));
        else if (definition->Duration != EffectDuration::Infinite)
            errors.push_back(std::format("equip_effect_not_maintained: '{}' is not Infinite, and equipment maintains "
                                         "its effects for exactly as long as it is worn",
                                         equipped.Effect));
        else if (!by.has_value())
            errors.push_back(std::format("equip.effects: scale_by '{}' names no number this item states",
                                         equipped.ScaleBy));
        else
            out.EquipEffects.push_back({ effect, equipped.Scale * static_cast<float>(*by) });
    }
    for (const ItemGrantParameters& granted : declaration.GrantParameters)
        if (!EquipEffectsGrant(world, out.EquipEffects, granted.Key))
            errors.push_back(std::format("grants: no equip effect grants '{}'", granted.Key));

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
            AbilityGrantEntryRefusal refusal = AbilityGrantEntryRefusal::Full;
            out.UseEntry = entries->Intern({ declaration.Use->Key, ability, declaration.Use->Override,
                                             declaration.Use->Parameters },
                                           &refusal);
            if (out.UseEntry == kAbilityNoGrantEntry)
                errors.push_back(std::format("use grant '{}' {}", declaration.Use->Key,
                                             AbilityGrantEntryRefusalText(refusal)));
        }
    }
    return errors.size() == before;
}
