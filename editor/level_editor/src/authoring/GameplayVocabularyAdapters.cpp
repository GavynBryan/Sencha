#include "GameplayVocabularyAdapters.h"

#include "GameplayVocabularyEdits.h"
#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"

#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
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

    // One undoable whole-component write. These components are small trivially
    // copyable values holding no handles, so a before/after pair is a complete
    // record and no lifecycle hook is owed anything.
    template <typename Component>
    PropertyEditResult Commit(EditorComponentInspectorContext& context, const Component& before,
                              const Component& after)
    {
        context.Scene.SetComponent(context.Entity, after);
        context.Commands.Execute(std::make_unique<ValueCommand<Component>>(
            before, after,
            [&scene = context.Scene, entity = context.Entity](const Component& value)
            { scene.SetComponent(entity, value); },
            context.Document));
        return PropertyEditResult::Applied;
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
        PropertyEditResult Report(EntityId entity, const VocabularyEdit& edit) const
        {
            Entity = entity;
            Message = edit.Error;
            if (!edit.Error.empty())
                return PropertyEditResult::Refused;
            return edit.Changed ? PropertyEditResult::Applied : PropertyEditResult::Unchanged;
        }

        void AppendTo(EntityId entity, std::vector<PropertyNode>& nodes) const
        {
            if (entity == Entity && !Message.empty())
                nodes.push_back(NoteProperty("refused", "Refused", Message));
        }

    private:
        mutable EntityId Entity;
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
                if (!held->HasExact(tag))
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
            Refusal.AppendTo(context.Entity, nodes);
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
            const PropertyEditResult result = Refusal.Report(context.Entity, edit);
            return result == PropertyEditResult::Applied ? Commit(context, *held, working) : result;
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
            const PropertyEditResult result = Refusal.Report(context.Entity, edit);
            return result == PropertyEditResult::Applied ? Commit(context, *held, working) : result;
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
            Refusal.AppendTo(context.Entity, nodes);
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
                context.Entity, SetAttributeBase(working, *registry, AttributeId{ *id }, static_cast<float>(*base)));
            return result == PropertyEditResult::Applied ? Commit(context, *held, working) : result;
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
            const PropertyEditResult result = Refusal.Report(context.Entity, edit);
            return result == PropertyEditResult::Applied ? Commit(context, *held, working) : result;
        }

    private:
        RefusalNote Refusal;
    };

    //=========================================================================
    // Abilities
    //=========================================================================
    class AbilitySetEditorAdapter final : public IEditorComponentAdapter
    {
    public:
        ComponentTypeId Type() const override
        {
            return ResolveComponentTypeId<AbilitySet>();
        }

        bool AuthorsInspectorRows() const override { return true; }

        std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext& context) const override
        {
            const World& world = std::as_const(context.Scene.GetRegistry()).Components;
            const AbilitySet* held = world.TryGet<AbilitySet>(context.Entity);
            const auto* registry = world.TryGetResource<AbilityRegistry>();
            if (held == nullptr)
                return {};
            if (registry == nullptr)
                return { NoteProperty("missing", "Abilities", "This document has no ability registry.") };

            PropertyNode abilities = GroupProperty("abilities", "Abilities");
            std::vector<std::string> offered;
            ForEachRegistered<AbilityId>(*registry, [&](AbilityId id) {
                if (!held->Has(id))
                    offered.emplace_back(registry->GetName(id));
            });
            if (!offered.empty())
                abilities.Descriptor.Verbs.push_back({ .Id = "grant", .Label = "Grant Ability", .Choices = std::move(offered) });
            for (std::uint8_t i = 0; i < held->Count; ++i)
            {
                const AbilityId id = held->Abilities[i];
                PropertyNode row = NoteProperty("ability:" + std::to_string(id.Value), DisplayName(registry->GetName(id)), "");
                row.Descriptor.Verbs.push_back({ .Id = "revoke", .Label = "Revoke" });
                abilities.Children.push_back(std::move(row));
            }
            std::vector<PropertyNode> nodes{ std::move(abilities) };
            Refusal.AppendTo(context.Entity, nodes);
            return nodes;
        }

        PropertyEditResult ActInspector(EditorComponentInspectorContext& context, std::string_view key,
                                        std::string_view verb, std::string_view choice) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const AbilitySet* held = std::as_const(world).TryGet<AbilitySet>(context.Entity);
            const auto* registry = world.TryGetResource<AbilityRegistry>();
            if (held == nullptr || registry == nullptr)
                return PropertyEditResult::Gone;
            AbilitySet working = *held;
            VocabularyEdit edit;
            if (key == "abilities" && verb == "grant")
            {
                std::optional<AbilityId> chosen;
                ForEachRegistered<AbilityId>(*registry, [&](AbilityId id) {
                    if (registry->GetName(id) == choice)
                        chosen = id;
                });
                if (!chosen.has_value())
                    return PropertyEditResult::Refused;
                edit = GrantAbility(working, *registry, *chosen);
            }
            else if (const std::optional<std::uint32_t> id = EntryOf(key, "ability:"); id.has_value() && verb == "revoke")
                edit = RevokeAbility(working, AbilityId{ *id });
            else
                return PropertyEditResult::Gone;
            const PropertyEditResult result = Refusal.Report(context.Entity, edit);
            return result == PropertyEditResult::Applied ? Commit(context, *held, working) : result;
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
            return Commit(context, *held, after);
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

std::unique_ptr<IEditorComponentAdapter> MakeAbilitySetEditorAdapter()
{
    return std::make_unique<AbilitySetEditorAdapter>();
}

std::unique_ptr<IEditorComponentAdapter> MakeCharacterMovementEditorAdapter()
{
    return std::make_unique<CharacterMovementEditorAdapter>();
}
