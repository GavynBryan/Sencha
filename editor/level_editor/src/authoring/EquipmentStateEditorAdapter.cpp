#include "EquipmentStateEditorAdapter.h"

#include "HeldEditorAdapter.h"
#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "properties/SettingProperties.h"

#include <equipment/Equipment.h>
#include <equipment/EquipmentComponents.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view kEmpty = "Empty";
    constexpr std::string_view kSlotPrefix = "slot:";

    struct EntryChoice
    {
        PersistableRef Entry;
        ItemId Definition;
    };

    // An entry the wearer holds whose item goes into the slot first.
    EntityReferenceFilter Wearable(const EditorComponentInspectorContext& context, EquipmentLayoutId layout,
                                   std::uint8_t slot)
    {
        return EntityReferenceFilter{
            .Within = &context.Document,
            .Admits = [wearer = context.Entity, layout, slot](const EntityReferenceSubject& subject) {
                const World& world = subject.Components;
                const ItemEntry* entry = world.IsRegistered<ItemEntry>() ? world.TryGet<ItemEntry>(subject.Entity)
                                                                         : nullptr;
                const ItemDefinition* item = entry != nullptr ? FindItemDefinition(world, entry->Definition) : nullptr;
                const EquipmentLayout* slots = FindEquipmentLayout(world, layout);
                return item != nullptr && slots != nullptr && subject.Entity != wearer
                    && IsHeldWithin(world, subject.Entity, wearer) && ItemTakesSlotFirst(world, *slots, slot, *item);
            } };
    }

    std::optional<EntryChoice> WearableEntry(EditorComponentInspectorContext& context, EquipmentLayoutId layout,
                                             std::uint8_t slot, std::string_view value)
    {
        const std::optional<PersistentEntityId> target = PersistentEntityIdFromString(value);
        const std::optional<EntityReferenceSubject> entry =
            target.has_value() ? AdmittedEntityReference(context.World, Wearable(context, layout, slot), *target)
                               : std::nullopt;
        if (!entry.has_value())
            return std::nullopt;
        return EntryChoice{ .Entry = PersistableRef{ .Entity = entry->Entity, .Target = *target },
                            .Definition = entry->Components.TryGet<ItemEntry>(entry->Entity)->Definition };
    }

    void RemoveRecordsWhere(EquipmentRecordList& worn, auto&& matches)
    {
        for (std::size_t i = worn.Records.size(); i-- > 0;)
            if (matches(worn.Records[i]))
            {
                worn.Records[i] = worn.Records.back();
                worn.Records.pop_back();
            }
    }

    bool Wear(const World& world, const EquipmentLayout& layout, EquipmentRecordList& worn, const EntryChoice& entry,
              std::uint8_t slot)
    {
        const ItemDefinition* item = FindItemDefinition(world, entry.Definition);
        RemoveRecordsWhere(worn, [&](const EquipmentRecord& record) { return record.Source.Target == entry.Entry.Target; });
        std::uint32_t slots = 0;
        std::uint8_t primary = kNoEquipmentSlot;
        if (item == nullptr
            || ResolveEquipmentSlots(world, layout, worn.Records, *item, layout.Slots[slot].Key, slots, primary)
                != EquipmentRefusal::None)
            return false;
        (void)WearRecord(worn.Records, worn.NextKey,
                         EquipmentRecord{ .Key = 0, .Definition = entry.Definition, .Slots = slots, .Primary = primary,
                                          .Source = entry.Entry, .Derivation = EquipmentDerivation::Pending });
        return true;
    }

    const EquipmentRecord* RecordHolding(std::span<const EquipmentRecord> records, std::uint8_t slot)
    {
        const std::size_t holding = FindEquipmentRecordHolding(records, slot);
        return holding != kNoEquipmentRecord ? &records[holding] : nullptr;
    }

    PropertyNode LayoutRow(const World& world, EquipmentLayoutId layout)
    {
        const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
        std::vector<std::string> names;
        if (layouts != nullptr)
            layouts->ForEachName([&](EquipmentLayoutId, std::string_view name) { names.emplace_back(name); });
        const std::string current(layouts != nullptr ? layouts->GetName(layout) : std::string_view{});
        if (std::ranges::find(names, current) == names.end())
            names.push_back(current);
        const std::vector<std::string_view> options(names.begin(), names.end());
        return ChoiceProperty("layout", "Layout", options,
                              static_cast<std::size_t>(std::ranges::find(names, current) - names.begin()),
                              "The slots it equips into, by the layout's asset. Changed only while nothing is worn.");
    }

    PropertyNode SlotRow(const EditorComponentInspectorContext& context, const EquipmentLayout& layout,
                         std::span<const EquipmentRecord> records, std::uint8_t slot)
    {
        const EquipmentRecord* record = RecordHolding(records, slot);
        const bool sourced = record != nullptr && record->Source.Target.IsValid();
        PropertyNode row = EntityReferenceProperty(std::string(kSlotPrefix) + layout.Slots[slot].Key,
                                                   layout.Slots[slot].Key,
                                                   sourced ? PersistentEntityIdToString(record->Source.Target)
                                                           : std::string{},
                                                   std::string(kEmpty),
                                                   "The entry worn here, among those it holds whose item goes into "
                                                   "this slot.");
        if (record != nullptr && !sourced)
        {
            row.Reading = "Waiting for another";
            row.Descriptor.Verbs.push_back({ .Id = std::string(EntityReferenceVerbs::Clear), .Label = "Clear" });
        }
        const World& world = std::as_const(context.Scene.GetRegistry()).Components;
        const EntityId source = sourced ? context.Scene.EntityWithPersistentId(record->Source.Target) : EntityId{};
        if (sourced && (!source.IsValid() || !IsHeldWithin(world, source, context.Entity)))
        {
            EntityReferenceReading reading = ReadEntityReference(context.World, row.Value);
            if (reading.State == EntityReferenceState::Loaded)
                reading.Text += ", not held here";
            ShowEntityReference(row, reading);
        }
        return row;
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
                    rows.push_back(SlotRow(context, *layout, EquipmentRecordsOf(world, *state), static_cast<std::uint8_t>(slot)));
            return rows;
        }

        std::optional<EntityReferenceFilter> ReferenceFilter(const EditorComponentInspectorContext& context,
                                                             std::string_view key) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const EquipmentState* state = world.TryGet<EquipmentState>(context.Entity);
            const EquipmentLayout* layout = state != nullptr ? FindEquipmentLayout(world, state->Layout) : nullptr;
            if (layout == nullptr || !key.starts_with(kSlotPrefix))
                return std::nullopt;
            const std::uint8_t slot = FindEquipmentSlot(*layout, key.substr(kSlotPrefix.size()));
            if (slot == kNoEquipmentSlot)
                return std::nullopt;
            return Wearable(context, state->Layout, slot);
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const EquipmentState* held = world.TryGet<EquipmentState>(context.Entity);
            if (held == nullptr)
                return PropertyEditResult::Gone;
            const EquipmentRecordList before = CopyEquipment(world, context.Entity);
            EquipmentRecordList after = before;
            if (key == "layout")
            {
                const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
                after.Layout = layouts != nullptr ? layouts->Find(value) : EquipmentLayoutId{};
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
                if (value.empty())
                    RemoveRecordsWhere(after, [&](const EquipmentRecord& record) { return ((record.Slots >> slot) & 1u) != 0; });
                else
                {
                    const std::optional<EntryChoice> chosen = WearableEntry(context, held->Layout, slot, value);
                    const EquipmentRecord* worn = RecordHolding(before.Records, slot);
                    if (chosen.has_value() && worn != nullptr && worn->Source.Target == chosen->Entry.Target)
                        return PropertyEditResult::Unchanged;
                    if (!chosen.has_value() || !Wear(world, *layout, after, *chosen, slot))
                        return PropertyEditResult::Refused;
                }
            }
            else
                return PropertyEditResult::Gone;
            if (after.Layout == before.Layout && after.Records.size() == before.Records.size()
                && after.NextKey == before.NextKey)
                return PropertyEditResult::Unchanged;
            context.Commands.Execute(std::make_unique<ValueCommand<EquipmentRecordList>>(
                before, after,
                [&scene = context.Scene, entity = context.Entity](const EquipmentRecordList& worn) {
                    SetEquipment(scene.GetRegistry().Components, entity, worn);
                },
                context.Document));
            return PropertyEditResult::Applied;
        }
    };
}

std::unique_ptr<IEditorComponentAdapter> MakeEquipmentStateEditorAdapter()
{
    return std::make_unique<EquipmentStateEditorAdapter>();
}
