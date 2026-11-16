#include "GameplayVocabularyAdapters.h"

#include "ComponentValueCommit.h"
#include "GameplayVocabularyEdits.h"
#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/InventoryComponents.h>
#include <movement/LocomotionMode.h>
#include <movement/components/CharacterMovement.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    // Every registry here is dense and 1-based: index 0 is a reserved slot so a
    // zero id is invalid, and Size() counts the real entries. Walking ids is
    // therefore the whole enumeration, with no table to build per frame.
    template <typename Id, typename Registry, typename Fn>
    void ForEachRegistered(const Registry& registry, Fn&& visit)
    {
        for (std::uint32_t value = 1; value <= registry.Size(); ++value)
            visit(Id{ value });
    }

    // A name the registry no longer knows, which authored content can outlive:
    // shown as itself rather than as a blank row.
    std::string DisplayName(std::string_view name)
    {
        return name.empty() ? std::string("<unregistered>") : std::string(name);
    }

    // "tag:12" -> 12, for a key naming a registry entry.
    std::optional<std::uint32_t> EntryOf(std::string_view key, std::string_view prefix)
    {
        const std::optional<PropertyKeyIndex> entry = ParsePropertyKeyIndex(key, prefix);
        if (!entry.has_value() || !entry->Rest.empty())
            return std::nullopt;
        return static_cast<std::uint32_t>(entry->Index);
    }

    // What a refused edit said, until the entity or the next edit changes it.
    class RefusalNote
    {
    public:
        PropertyEditResult Report(const SelectableRef& entity, const VocabularyEdit& edit) const
        {
            Entity = entity;
            Message = edit.Error;
            if (!edit.Error.empty())
                return PropertyEditResult::Refused;
            return edit.Changed ? PropertyEditResult::Applied : PropertyEditResult::Unchanged;
        }

        void AppendTo(const SelectableRef& entity, std::vector<PropertyNode>& nodes) const
        {
            if (entity == Entity && !Message.empty())
                nodes.push_back(NoteProperty("refused", "Refused", Message));
        }

    private:
        mutable SelectableRef Entity;
        mutable std::string Message;
    };

    //=========================================================================
    // Gameplay tags
    //=========================================================================
    class GameplayTagEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<GameplayTagContainer>();
        }

        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const GameplayTagContainer* held = world.TryGet<GameplayTagContainer>(context.Entity);
            const auto* registry = world.TryGetResource<GameplayTagRegistry>();
            if (held == nullptr)
                return {};
            if (registry == nullptr)
                return { NoteProperty("missing", "Tags", "This document has no gameplay tag registry.") };

            PropertyNode tags = GroupProperty("tags", "Tags");
            std::vector<std::string> offered;
            ForEachRegistered<GameplayTagId>(*registry, [&](GameplayTagId tag) {
                if (!held->HasExact(tag) && registry->ProjectedBy(tag).empty())
                    offered.emplace_back(registry->GetName(tag));
            });
            if (!offered.empty())
                tags.Descriptor.Verbs.push_back({ .Id = "grant", .Label = "Grant Tag", .Choices = std::move(offered) });
            for (std::uint8_t i = 0; i < held->Count; ++i)
            {
                const GameplayTagId tag = held->Tags[i];
                PropertyNode row = NumberProperty("tag:" + std::to_string(tag.Value),
                                                  DisplayName(registry->GetName(tag)), held->Counts[i], 0.0, -1.0, 1.0,
                                                  "How many sources grant this tag. It stays present until every "
                                                  "one of them revokes it.");
                row.Descriptor.Verbs.push_back({ .Id = "revoke", .Label = "Revoke" });
                tags.Children.push_back(std::move(row));
            }
            PropertyNode typed;
            typed.Key = "new";
            typed.Descriptor.Kind = PropertyKind::Text;
            typed.Descriptor.Label = "New tag";
            typed.Descriptor.Tooltip = "A dot-path (State.Stunned); a name the registry lacks is declared";
            tags.Children.push_back(std::move(typed));

            std::vector<PropertyNode> nodes{ std::move(tags) };
            Refusal.AppendTo(context.Ref(), nodes);
            return nodes;
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const GameplayTagContainer* held = std::as_const(world).TryGet<GameplayTagContainer>(context.Entity);
            auto* registry = world.TryGetResource<GameplayTagRegistry>();
            if (held == nullptr || registry == nullptr)
                return PropertyEditResult::Gone;
            GameplayTagContainer working = *held;
            VocabularyEdit edit;
            if (key == "new")
            {
                if (value.empty())
                    return PropertyEditResult::Unchanged;
                edit = GrantTagByName(working, *registry, value);
            }
            else if (const std::optional<std::uint32_t> tag = EntryOf(key, "tag:"))
            {
                const std::optional<double> stacks = ParseNumber(value, 1.0, 999.0);
                if (!stacks.has_value())
                    return PropertyEditResult::Refused;
                edit = SetTagStacks(working, GameplayTagId{ *tag }, static_cast<std::uint16_t>(std::lround(*stacks)));
            }
            else
            {
                return PropertyEditResult::Gone;
            }
            const PropertyEditResult result = Refusal.Report(context.Ref(), edit);
            return result == PropertyEditResult::Applied ? CommitComponentValue(context, *held, working) : result;
        }

        PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                        std::string_view verb, std::string_view choice) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const GameplayTagContainer* held = std::as_const(world).TryGet<GameplayTagContainer>(context.Entity);
            auto* registry = world.TryGetResource<GameplayTagRegistry>();
            if (held == nullptr || registry == nullptr)
                return PropertyEditResult::Gone;
            GameplayTagContainer working = *held;
            VocabularyEdit edit;
            if (key == "tags" && verb == "grant")
                edit = GrantTagByName(working, *registry, choice);
            else if (const std::optional<std::uint32_t> tag = EntryOf(key, "tag:"); tag.has_value() && verb == "revoke")
                edit = RevokeTag(working, GameplayTagId{ *tag });
            else
                return PropertyEditResult::Gone;
            const PropertyEditResult result = Refusal.Report(context.Ref(), edit);
            return result == PropertyEditResult::Applied ? CommitComponentValue(context, *held, working) : result;
        }

    private:
        RefusalNote Refusal;
    };

    //=========================================================================
    // Attributes
    //=========================================================================
    class AttributeSetEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<AttributeSet>();
        }

        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const AttributeSet* held = world.TryGet<AttributeSet>(context.Entity);
            const auto* registry = world.TryGetResource<AttributeRegistry>();
            if (held == nullptr)
                return {};
            if (registry == nullptr)
                return { NoteProperty("missing", "Attributes", "This document has no attribute registry.") };

            PropertyNode attributes = GroupProperty("attributes", "Attributes");
            std::vector<std::string> offered;
            ForEachRegistered<AttributeId>(*registry, [&](AttributeId id) {
                if (!held->Has(id))
                    offered.emplace_back(registry->GetName(id));
            });
            if (!offered.empty())
                attributes.Descriptor.Verbs.push_back({ .Id = "add", .Label = "Add Attribute", .Choices = std::move(offered) });
            for (std::uint8_t i = 0; i < held->Count; ++i)
            {
                const AttributeId id = held->Ids[i];
                PropertyNode row = NumberProperty("attr:" + std::to_string(id.Value), DisplayName(registry->GetName(id)),
                                                  held->Base[i], 0.0, -1.0, 0.05,
                                                  "The authored value. What the entity has in play is this folded "
                                                  "with active effects.");
                row.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove" });
                attributes.Children.push_back(std::move(row));
            }
            std::vector<PropertyNode> nodes{ std::move(attributes) };
            Refusal.AppendTo(context.Ref(), nodes);
            return nodes;
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const AttributeSet* held = std::as_const(world).TryGet<AttributeSet>(context.Entity);
            const auto* registry = world.TryGetResource<AttributeRegistry>();
            const std::optional<std::uint32_t> id = EntryOf(key, "attr:");
            if (held == nullptr || registry == nullptr || !id.has_value())
                return PropertyEditResult::Gone;
            const std::optional<double> base = ParseNumber(value);
            if (!base.has_value())
                return PropertyEditResult::Refused;
            AttributeSet working = *held;
            const PropertyEditResult result = Refusal.Report(
                context.Ref(), SetAttributeBase(working, *registry, AttributeId{ *id }, static_cast<float>(*base)));
            return result == PropertyEditResult::Applied ? CommitComponentValue(context, *held, working) : result;
        }

        PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                        std::string_view verb, std::string_view choice) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const AttributeSet* held = std::as_const(world).TryGet<AttributeSet>(context.Entity);
            const auto* registry = world.TryGetResource<AttributeRegistry>();
            if (held == nullptr || registry == nullptr)
                return PropertyEditResult::Gone;
            AttributeSet working = *held;
            VocabularyEdit edit;
            if (key == "attributes" && verb == "add")
            {
                std::optional<AttributeId> chosen;
                ForEachRegistered<AttributeId>(*registry, [&](AttributeId id) {
                    if (registry->GetName(id) == choice)
                        chosen = id;
                });
                if (!chosen.has_value())
                    return PropertyEditResult::Refused;
                edit = AddAttribute(working, *registry, *chosen);
            }
            else if (const std::optional<std::uint32_t> id = EntryOf(key, "attr:"); id.has_value() && verb == "remove")
                edit = RemoveAttribute(working, AttributeId{ *id });
            else
                return PropertyEditResult::Gone;
            const PropertyEditResult result = Refusal.Report(context.Ref(), edit);
            return result == PropertyEditResult::Applied ? CommitComponentValue(context, *held, working) : result;
        }

    private:
        RefusalNote Refusal;
    };

    //=========================================================================
    // Abilities
    //=========================================================================
    class AbilityGrantsEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<AbilityGrants>();
        }

        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const AbilityGrants* held = world.TryGet<AbilityGrants>(context.Entity);
            const auto* registry = world.TryGetResource<AbilityRegistry>();
            if (held == nullptr)
                return {};
            if (registry == nullptr)
                return { NoteProperty("missing", "Abilities", "This document has no ability registry.") };

            PropertyNode abilities = GroupProperty("abilities", "Abilities");
            std::vector<std::string> offered;
            ForEachRegistered<AbilityId>(*registry, [&](AbilityId id) {
                if (registry->Get(id) != nullptr)
                    offered.emplace_back(registry->GetName(id));
            });
            if (!offered.empty())
                abilities.Descriptor.Verbs.push_back({ .Id = "grant", .Label = "Grant Ability", .Choices = std::move(offered) });
            for (std::uint8_t i = 0; i < held->Count; ++i)
            {
                const AbilityGrant& grant = held->Entries[i];
                const AbilityDefinition* definition = registry->Get(grant.Ability);
                const std::string name = definition != nullptr ? definition->DisplayName
                                                               : std::string(registry->GetName(grant.Ability));
                PropertyNode row = NoteProperty("grant:" + std::to_string(grant.Id.Value), name,
                                                definition != nullptr ? "" : "This ability is not declared here.");
                row.Descriptor.Verbs.push_back({ .Id = "revoke", .Label = "Revoke" });
                abilities.Children.push_back(std::move(row));
            }
            std::vector<PropertyNode> nodes{ std::move(abilities) };
            Refusal.AppendTo(context.Ref(), nodes);
            return nodes;
        }

        PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                        std::string_view verb, std::string_view choice) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const AbilityGrants* held = std::as_const(world).TryGet<AbilityGrants>(context.Entity);
            const auto* registry = world.TryGetResource<AbilityRegistry>();
            auto* entries = world.TryGetResource<AbilityGrantEntries>();
            if (held == nullptr || registry == nullptr || entries == nullptr)
                return PropertyEditResult::Gone;
            AbilityGrants working = *held;
            VocabularyEdit edit;
            if (key == "abilities" && verb == "grant")
            {
                const AbilityId chosen = registry->Find(choice);
                if (!chosen.IsValid())
                    return PropertyEditResult::Refused;
                edit = AddAbilityGrant(working, *entries, *registry, chosen);
            }
            else if (const std::optional<std::uint32_t> id = EntryOf(key, "grant:"); id.has_value() && verb == "revoke")
                edit = RemoveAbilityGrant(working, AbilityGrantId{ *id });
            else
                return PropertyEditResult::Gone;
            const PropertyEditResult result = Refusal.Report(context.Ref(), edit);
            return result == PropertyEditResult::Applied ? CommitComponentValue(context, *held, working) : result;
        }

    private:
        RefusalNote Refusal;
    };

    //=========================================================================
    // Locomotion mode
    //=========================================================================
    class CharacterMovementEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<CharacterMovement>();
        }

        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const CharacterMovement* held = world.TryGet<CharacterMovement>(context.Entity);
            const auto* modes = world.TryGetResource<LocomotionModeRegistry>();
            if (held == nullptr)
                return {};
            if (modes == nullptr)
                return { NoteProperty("missing", "Starting mode", "This document has no locomotion mode registry.") };

            PropertyNode mode;
            mode.Key = "mode";
            mode.Descriptor.Kind = PropertyKind::Choice;
            mode.Descriptor.Label = "Starting mode";
            mode.Descriptor.Tooltip = "Which locomotion rules the character spawns under. Modes change during play; "
                                      "this is only where it begins.";
            for (const LocomotionModeEntry& entry : modes->Entries())
                mode.Descriptor.Choices.push_back(entry.Name);
            const LocomotionModeEntry* current = modes->Find(held->Mode);
            mode.Value = current != nullptr ? current->Name : std::string("<unregistered>");
            if (current == nullptr)
                mode.Descriptor.Choices.push_back(mode.Value);
            return { std::move(mode) };
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const CharacterMovement* held = std::as_const(world).TryGet<CharacterMovement>(context.Entity);
            const auto* modes = world.TryGetResource<LocomotionModeRegistry>();
            if (key != "mode" || held == nullptr || modes == nullptr)
                return PropertyEditResult::Gone;
            CharacterMovement after = *held;
            if (!SetLocomotionMode(after, *modes, value).Changed)
                return PropertyEditResult::Unchanged;
            return CommitComponentValue(context, *held, after);
        }
    };

    constexpr std::string_view kAnything = "Anything";

    PropertyNode ItemDefinitionRow(const World& world, ItemId definition)
    {
        const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
        std::vector<std::string> names;
        if (items != nullptr)
            ForEachRegistered<ItemId>(*items, [&](ItemId id) { names.push_back(DisplayName(items->GetName(id))); });
        const std::string current = DisplayName(items != nullptr ? items->GetName(definition) : std::string_view{});
        if (std::ranges::find(names, current) == names.end())
            names.push_back(current);
        const std::vector<std::string_view> options(names.begin(), names.end());
        return ChoiceProperty("definition", "Item", options,
                              static_cast<std::size_t>(std::ranges::find(names, current) - names.begin()),
                              "Which item this is, by the asset that declares it.");
    }

    template <typename Counted>
    class CountedItemEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        explicit CountedItemEditorAdapter(std::string_view countTooltip)
            : CountTooltip(countTooltip)
        {
        }

        ComponentTypeId Type() const override { return ResolveComponentTypeId<Counted>(); }
        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const Counted* held = world.TryGet<Counted>(context.Entity);
            if (held == nullptr)
                return {};
            return { ItemDefinitionRow(world, held->Definition),
                     NumberProperty("count", "Count", static_cast<double>(held->Count), 0.0, 4294967295.0, 1.0,
                                    std::string(CountTooltip)) };
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const Counted* held = std::as_const(world).TryGet<Counted>(context.Entity);
            if (held == nullptr)
                return PropertyEditResult::Gone;
            Counted after = *held;
            if (key == "definition")
            {
                const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
                after.Definition = items != nullptr ? items->Find(value) : ItemId{};
                if (!after.Definition.IsValid())
                    return PropertyEditResult::Refused;
            }
            else if (key == "count")
            {
                const std::optional<double> count = ParseNumber(value, 0.0, 4294967295.0);
                if (!count)
                    return PropertyEditResult::Refused;
                after.Count = static_cast<std::uint32_t>(*count);
            }
            else
                return PropertyEditResult::Gone;
            if (after.Definition == held->Definition && after.Count == held->Count)
                return PropertyEditResult::Unchanged;
            return CommitComponentValue(context, *held, after);
        }

    private:
        std::string_view CountTooltip;
    };

    class ItemContainerEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override { return ResolveComponentTypeId<ItemContainer>(); }
        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const ItemContainer* container = world.TryGet<ItemContainer>(context.Entity);
            if (container == nullptr)
                return {};
            std::vector<std::string> names{ std::string(kAnything) };
            std::size_t current = 0;
            if (const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
                ForEachRegistered<GameplayTagId>(*tags, [&](GameplayTagId tag) {
                    if (tag == container->Accepts)
                        current = names.size();
                    names.emplace_back(tags->GetName(tag));
                });
            const std::vector<std::string_view> options(names.begin(), names.end());
            return { NumberProperty("capacity", "Holds", static_cast<double>(container->Capacity), 0.0, 65535.0, 1.0,
                                    "Entries it holds at most; zero is as many as a capacity can say."),
                     ChoiceProperty("accepts", "Accepts", options, current,
                                    "An item must carry this tag, or one beneath it.") };
        }

        PropertyEditResult SetInspectorValue(EditorComponentInspectorContext& context, std::string_view key,
                                             std::string_view value) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const ItemContainer* held = std::as_const(world).TryGet<ItemContainer>(context.Entity);
            if (held == nullptr)
                return PropertyEditResult::Gone;
            ItemContainer after = *held;
            if (key == "capacity")
            {
                const std::optional<double> capacity = ParseNumber(value, 0.0, 65535.0);
                if (!capacity)
                    return PropertyEditResult::Refused;
                after.Capacity = static_cast<std::uint16_t>(*capacity);
            }
            else if (key == "accepts")
            {
                const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
                after.Accepts = value == kAnything || tags == nullptr ? GameplayTagId{} : tags->FindTag(value);
                if (value != kAnything && !after.Accepts.IsValid())
                    return PropertyEditResult::Refused;
            }
            else
                return PropertyEditResult::Gone;
            if (after.Capacity == held->Capacity && after.Accepts == held->Accepts)
                return PropertyEditResult::Unchanged;
            return CommitComponentValue(context, *held, after);
        }
    };
}

std::unique_ptr<IEditorComponentAdapter> MakeGameplayTagEditorAdapter()
{
    return std::make_unique<GameplayTagEditorAdapter>();
}

std::unique_ptr<IEditorComponentAdapter> MakeAttributeSetEditorAdapter()
{
    return std::make_unique<AttributeSetEditorAdapter>();
}

std::unique_ptr<IEditorComponentAdapter> MakeAbilityGrantsEditorAdapter()
{
    return std::make_unique<AbilityGrantsEditorAdapter>();
}

std::unique_ptr<IEditorComponentAdapter> MakeCharacterMovementEditorAdapter()
{
    return std::make_unique<CharacterMovementEditorAdapter>();
}

std::unique_ptr<IEditorComponentAdapter> MakeItemEntryEditorAdapter()
{
    return std::make_unique<CountedItemEditorAdapter<ItemEntry>>("How many units this entry holds; zero is spent.");
}

std::unique_ptr<IEditorComponentAdapter> MakeItemContainerEditorAdapter()
{
    return std::make_unique<ItemContainerEditorAdapter>();
}

std::unique_ptr<IEditorComponentAdapter> MakeItemPickupEditorAdapter()
{
    return std::make_unique<CountedItemEditorAdapter<ItemPickup>>(
        "How many units taking it gives, when it holds no entry of its own.");
}
