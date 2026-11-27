#include <equipment/EquipmentLayoutData.h>

#include <core/json/JsonValue.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <input/InputActionRegistry.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <utility>

namespace
{
    DataSchema MakeSchema()
    {
        DataFieldSchema slot = MakeDataField(DataFieldKind::Record, "slot", "Slot");
        slot.Children.push_back(MakeDataField(DataFieldKind::String, "key", "Key",
                                              "Its name; a '.' puts it in a group (ring.left is in ring)."));
        slot.Children.push_back(MakeOptionalDataField(DataFieldKind::GameplayTag, "accepts", "Accepts",
                                                      "An item must carry this tag, or one beneath it."));
        slot.Children.push_back(MakeOptionalDataField(DataFieldKind::String, "action", "Action",
                                                      "The input that casts what an item equipped here grants."));
        slot.Children.push_back(MakeOptionalDataField(DataFieldKind::Int, "priority", "Priority",
                                                      "Which slot's grant an action casts when several bind it."));
        slot.Children.push_back(MakeOptionalDataField(DataFieldKind::Int, "order", "Order", "Where it is listed."));
        slot.Children.push_back(MakeOptionalDataField(DataFieldKind::String, "socket", "Socket",
                                                      "Where what it holds is shown on the body."));
        DataFieldSchema depleted = MakeOptionalDataField(DataFieldKind::Enum, "on_depleted", "When spent",
                                                         "What the slot does when the entry it holds is spent.");
        depleted.EnumChoices = { { "release", "Release", "The item is unequipped." },
                                 { "hold", "Hold", "The slot keeps it, and the next one picked up takes its place." } };
        depleted.Default = std::string("release");
        slot.Children.push_back(std::move(depleted));

        DataFieldSchema slots = MakeDataField(DataFieldKind::Array, "slots", "Slots");
        slots.MergeKey = "key";
        slots.Editor.Widget = "cards";
        slots.Children.push_back(std::move(slot));

        DataFieldSchema locks = MakeOptionalDataField(DataFieldKind::Array, "lock_tags", "Locked while",
                                                      "While the wearer holds any of these, nothing is swapped.");
        locks.Children.push_back(MakeDataField(DataFieldKind::GameplayTag, "tag", "Tag"));

        DataSchema schema;
        schema.TypeName = std::string(kEquipmentLayoutType);
        schema.DisplayName = "Equipment Layout";
        schema.Description = "The slots something equips into, and what each does with what it holds.";
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children = { std::move(slots), std::move(locks) };
        return schema;
    }

    bool IsSlotKey(std::string_view key)
    {
        return !key.empty() && key.front() != '.' && key.back() != '.' && key.find("..") == std::string_view::npos
            && std::ranges::none_of(key, [](char c) { return c == ' ' || c == '\t' || c == '/'; });
    }

    std::optional<std::string> TextOf(const JsonValue& object, std::string_view key)
    {
        const JsonValue* value = object.Find(key);
        return value != nullptr && value->IsString() ? std::optional(value->AsString()) : std::nullopt;
    }

    DataAssetCompileResult Compile(const JsonValue& data)
    {
        auto value = std::make_shared<EquipmentLayoutDeclaration>();
        const JsonValue* slots = data.Find("slots");
        if (slots == nullptr || !slots->IsArray() || slots->AsArray().empty())
            return { {}, {}, "$.data.slots A layout has at least one slot." };
        if (slots->AsArray().size() > kMaxEquipmentSlots)
            return { {}, {}, std::format("$.data.slots A layout has at most {} slots.", kMaxEquipmentSlots) };
        for (std::size_t i = 0; i < slots->AsArray().size(); ++i)
        {
            const JsonValue& entry = slots->AsArray()[i];
            const std::string at = std::format("$.data.slots[{}]", i);
            EquipmentLayoutDeclaration::Slot slot;
            slot.Key = TextOf(entry, "key").value_or(std::string{});
            if (!IsSlotKey(slot.Key))
                return { {}, {}, at + ".key A slot is named, without spaces, and a '.' only between names." };
            if (std::ranges::any_of(value->Slots, [&](const auto& other) { return other.Key == slot.Key; }))
                return { {}, {}, std::format("{}.key '{}' names two slots.", at, slot.Key) };
            slot.Accepts = TextOf(entry, "accepts").value_or(std::string{});
            slot.Action = TextOf(entry, "action");
            if (const JsonValue* priority = entry.Find("priority"); priority != nullptr && priority->IsNumber())
                slot.Priority = static_cast<std::int32_t>(std::lround(priority->AsNumber()));
            if (const JsonValue* order = entry.Find("order"); order != nullptr && order->IsNumber())
                slot.Order = static_cast<std::int32_t>(std::lround(order->AsNumber()));
            slot.Socket = TextOf(entry, "socket").value_or(std::string{});
            const std::string depleted = TextOf(entry, "on_depleted").value_or("release");
            if (depleted != "release" && depleted != "hold")
                return { {}, {}, at + ".on_depleted A slot releases or holds what is spent." };
            slot.OnDepleted = depleted == "hold" ? SlotDepletion::Hold : SlotDepletion::Release;
            value->Slots.push_back(std::move(slot));
        }
        // EquipmentSlotsNamed takes the slot, so the group would be unreachable.
        for (std::size_t i = 0; i < value->Slots.size(); ++i)
        {
            const std::string& key = value->Slots[i].Key;
            const auto member = std::ranges::find_if(value->Slots, [&](const auto& other) {
                return other.Key.size() > key.size() && other.Key.starts_with(key) && other.Key[key.size()] == '.';
            });
            if (member != value->Slots.end())
                return { {}, {}, std::format("$.data.slots[{}].key '{}' is a slot and the group of '{}'.", i, key,
                                             member->Key) };
        }
        if (const JsonValue* locks = data.Find("lock_tags"); locks != nullptr && locks->IsArray())
            for (const JsonValue& tag : locks->AsArray())
            {
                if (!tag.IsString() || tag.AsString().empty())
                    return { {}, {}, "$.data.lock_tags A tag is a name." };
                value->LockTags.push_back(tag.AsString());
            }
        return { std::move(value), {}, {} };
    }

    GameplayTagId TagNamed(const GameplayTagRegistry* tags, const std::string& name, std::vector<std::string>& errors)
    {
        const GameplayTagId tag = tags != nullptr ? tags->FindTag(name) : GameplayTagId{};
        if (!tag.IsValid())
            errors.push_back(std::format("no gameplay tag '{}'", name));
        return tag;
    }
}

void RegisterEquipmentLayoutData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas)
{
    if (!types.Register({ std::string(kEquipmentLayoutType), 1, Compile }))
        return;
    if (!schemas.Register(MakeSchema()))
        (void)types.Unregister(kEquipmentLayoutType);
}

bool ResolveEquipmentLayoutDeclaration(const EquipmentLayoutDeclaration& declaration, const World& world,
                                       EquipmentLayout& out, std::vector<std::string>& errors)
{
    const std::size_t before = errors.size();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    const InputActionRegistry* actions = world.TryGetResource<InputActionRegistry>();
    out = EquipmentLayout{};
    for (const EquipmentLayoutDeclaration::Slot& declared : declaration.Slots)
    {
        EquipmentSlot slot;
        slot.Key = declared.Key;
        if (!declared.Accepts.empty())
            slot.Accepts = TagNamed(tags, declared.Accepts, errors);
        if (declared.Action && actions != nullptr && !actions->Find(*declared.Action).IsValid())
            errors.push_back(std::format("slot '{}': no input action '{}'", declared.Key, *declared.Action));
        slot.Binding.Action = declared.Action;
        slot.Binding.Priority = declared.Priority;
        slot.Order = declared.Order;
        slot.Socket = declared.Socket;
        slot.OnDepleted = declared.OnDepleted;
        out.Slots.push_back(std::move(slot));
    }
    for (const std::string& name : declaration.LockTags)
        if (const GameplayTagId tag = TagNamed(tags, name, errors); tag.IsValid())
            out.LockTags.push_back(tag);
    return errors.size() == before;
}
