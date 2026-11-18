#include "EquipmentStateEditorAdapter.h"

#include "ComponentValueCommit.h"
#include "HeldEditorAdapter.h"
#include "document/EntityLabel.h"
#include "properties/SettingProperties.h"

#include <ecs/Query.h>
#include <equipment/Equipment.h>
#include <equipment/EquipmentComponents.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kEmpty = "Empty";
    constexpr std::string_view kSlotPrefix = "slot:";

    struct EntryChoice
    {
        std::string Label;
        PersistableRef Entry;
        ItemId Definition;
    };

    std::string EntryLabel(const EditorDocument& document, EntityId entity, PersistentEntityId id)
    {
        return std::format("{} ({})", EntityLabel(document, entity), PersistentEntityIdToString(id));
    }

    // The identified entries the wearer holds whose item goes into `slot` first.
    std::vector<EntryChoice> EntryChoices(const EditorComponentInspectorContext& context, const EquipmentLayout& layout,
                                          std::uint8_t slot)
    {
        const World& world = std::as_const(context.Scene.GetRegistry()).Components;
        std::vector<EntryChoice> choices;
        if (!world.IsRegistered<ItemEntry>() || !world.IsRegistered<PersistentIdComponent>())
            return choices;
        Query<Read<ItemEntry>, Read<PersistentIdComponent>> entries(world);
        entries.ForEachChunk([&](auto& view) {
            const auto items = view.template Read<ItemEntry>();
            const auto ids = view.template Read<PersistentIdComponent>();
            for (std::uint32_t row = 0; row < view.Count(); ++row)
            {
                const EntityId entity = view.Entity(row);
                const ItemDefinition* item = FindItemDefinition(world, items[row].Definition);
                if (item == nullptr || item->Occupies.empty() || !ids[row].Id.IsValid() || entity == context.Entity
                    || !IsHeldWithin(world, entity, context.Entity))
                    continue;
                const std::vector<std::uint8_t> named = EquipmentSlotsNamed(layout, item->Occupies.front());
                if (std::ranges::find(named, slot) != named.end() && ItemCarries(world, *item, layout.Slots[slot].Accepts))
                    choices.push_back({ EntryLabel(context.Document, entity, ids[row].Id),
                                        PersistableRef{ .Entity = entity, .Target = ids[row].Id }, items[row].Definition });
            }
        });
        std::ranges::sort(choices, {}, &EntryChoice::Label);
        return choices;
    }

    void RemoveRecord(EquipmentState& state, std::size_t index)
    {
        state.Records[index] = state.Records[state.Count - 1];
        state.Records[--state.Count] = EquipmentRecord{};
    }

    void RemoveRecordsWhere(EquipmentState& state, auto&& matches)
    {
        for (std::size_t i = state.Count; i-- > 0;)
            if (matches(state.Records[i]))
                RemoveRecord(state, i);
    }

    // The record an equip of `entry` into `slot` would leave, replacing what holds its slots.
    bool Wear(const World& world, const EquipmentLayout& layout, EquipmentState& state, const EntryChoice& entry,
              std::uint8_t slot)
    {
        const ItemDefinition* item = FindItemDefinition(world, entry.Definition);
        RemoveRecordsWhere(state, [&](const EquipmentRecord& record) { return record.Source.Target == entry.Entry.Target; });
        std::uint32_t slots = 0;
        std::uint8_t primary = kNoEquipmentSlot;
        if (item == nullptr
            || ResolveEquipmentSlots(world, layout, state, *item, layout.Slots[slot].Key, slots, primary)
                != EquipmentRefusal::None)
            return false;
        RemoveRecordsWhere(state, [&](const EquipmentRecord& record) { return (record.Slots & slots) != 0; });
        if (state.Count == kEquipmentRecordCapacity)
            return false;
        state.Records[state.Count++] = EquipmentRecord{ .Key = state.NextKey++, .Definition = entry.Definition,
                                                        .Slots = slots, .Primary = primary, .Source = entry.Entry,
                                                        .Derivation = EquipmentDerivation::Pending };
        return true;
    }

    const EquipmentRecord* RecordHolding(const EquipmentState& state, std::uint8_t slot)
    {
        const auto found = std::ranges::find_if(state.Records, state.Records + state.Count, [&](const EquipmentRecord& record) {
            return ((record.Slots >> slot) & 1u) != 0;
        });
        return found != state.Records + state.Count ? found : nullptr;
    }

    PropertyNode LayoutRow(const World& world, EquipmentLayoutId layout)
    {
        const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
        std::vector<std::string> names;
        for (std::uint32_t id = 1; layouts != nullptr && id <= layouts->Size(); ++id)
            names.emplace_back(layouts->GetName(EquipmentLayoutId{ id }));
        const std::string current(layouts != nullptr ? layouts->GetName(layout) : std::string_view{});
        if (std::ranges::find(names, current) == names.end())
            names.push_back(current);
        const std::vector<std::string_view> options(names.begin(), names.end());
        return ChoiceProperty("layout", "Layout", options,
                              static_cast<std::size_t>(std::ranges::find(names, current) - names.begin()),
                              "The slots it equips into, by the layout's asset. Changed only while nothing is worn.");
    }

    PropertyNode SlotRow(const EditorComponentInspectorContext& context, const EquipmentLayout& layout,
                         const EquipmentState& state, std::uint8_t slot)
    {
        const std::vector<EntryChoice> choices = EntryChoices(context, layout, slot);
        std::vector<std::string> labels{ std::string(kEmpty) };
        for (const EntryChoice& choice : choices)
            labels.push_back(choice.Label);
        std::size_t current = 0;
        if (const EquipmentRecord* record = RecordHolding(state, slot))
        {
            const auto worn = std::ranges::find(choices, record->Source.Target,
                                                [](const EntryChoice& choice) { return choice.Entry.Target; });
            current = labels.size();
            if (worn != choices.end())
                current = 1 + static_cast<std::size_t>(worn - choices.begin());
            else
                labels.push_back(record->Source.Target.IsValid()
                                     ? std::format("Not held here ({})", PersistentEntityIdToString(record->Source.Target))
                                     : std::string("Waiting for another"));
        }
        const std::vector<std::string_view> options(labels.begin(), labels.end());
        return ChoiceProperty(std::string(kSlotPrefix) + layout.Slots[slot].Key, layout.Slots[slot].Key, options, current,
                              "The entry worn here, among those it holds whose item goes into this slot.");
    }

    class EquipmentStateEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override { return ResolveComponentTypeId<EquipmentState>(); }
        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const EquipmentState* state = world.TryGet<EquipmentState>(context.Entity);
            if (state == nullptr)
                return {};
            std::vector<PropertyNode> rows{ LayoutRow(world, state->Layout) };
            if (const EquipmentLayout* layout = FindEquipmentLayout(world, state->Layout))
                for (std::size_t slot = 0; slot < layout->Slots.size(); ++slot)
                    rows.push_back(SlotRow(context, *layout, *state, static_cast<std::uint8_t>(slot)));
            return rows;
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const EquipmentState* held = world.TryGet<EquipmentState>(context.Entity);
            if (held == nullptr)
                return PropertyEditResult::Gone;
            EquipmentState after = *held;
            if (key == "layout")
            {
                const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
                after.Layout = layouts != nullptr ? layouts->Find(value) : EquipmentLayoutId{};
                // Its records name slots of the layout they were made in.
                if (!after.Layout.IsValid() || (held->Count > 0 && after.Layout != held->Layout))
                    return PropertyEditResult::Refused;
            }
            else if (key.starts_with(kSlotPrefix))
            {
                const EquipmentLayout* layout = FindEquipmentLayout(world, held->Layout);
                const std::uint8_t slot =
                    layout != nullptr ? FindEquipmentSlot(*layout, key.substr(kSlotPrefix.size())) : kNoEquipmentSlot;
                if (slot == kNoEquipmentSlot)
                    return PropertyEditResult::Gone;
                if (value == kEmpty)
                    RemoveRecordsWhere(after, [&](const EquipmentRecord& record) { return ((record.Slots >> slot) & 1u) != 0; });
                else
                {
                    const std::vector<EntryChoice> choices = EntryChoices(context, *layout, slot);
                    const auto chosen = std::ranges::find(choices, value, &EntryChoice::Label);
                    const EquipmentRecord* worn = RecordHolding(*held, slot);
                    if (chosen != choices.end() && worn != nullptr && worn->Source.Target == chosen->Entry.Target)
                        return PropertyEditResult::Unchanged;
                    if (chosen == choices.end() || !Wear(world, *layout, after, *chosen, slot))
                        return PropertyEditResult::Refused;
                }
            }
            else
                return PropertyEditResult::Gone;
            if (after.Layout == held->Layout && after.Count == held->Count && after.NextKey == held->NextKey)
                return PropertyEditResult::Unchanged;
            return CommitComponentValue(context, *held, after);
        }
    };
}

std::unique_ptr<IEditorComponentAdapter> MakeEquipmentStateEditorAdapter()
{
    return std::make_unique<EquipmentStateEditorAdapter>();
}
