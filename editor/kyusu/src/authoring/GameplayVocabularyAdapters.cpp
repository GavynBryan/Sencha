#include "GameplayVocabularyAdapters.h"

#include "GameplayVocabularyEdits.h"
#include "commands/CommandStack.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"
#include "document/commands/ValueCommand.h"
#include "fonts/IconsFontAwesome6.h"

#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementComponents.h>

#include <imgui.h>

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
    void Commit(EditorComponentInspectorContext& context,
                const Component& before, const Component& after)
    {
        context.Commands.Execute(std::make_unique<ValueCommand<Component>>(
            before, after,
            [&scene = context.Scene, entity = context.Entity](const Component& value)
            { scene.SetComponent(entity, value); },
            context.Document));
    }

    // One drag in flight: the component as it stood before the gesture started,
    // so a drag records a single command rather than one per frame. Only one
    // widget is active at a time, so one slot is enough.
    template <typename Component>
    struct DragGesture
    {
        EntityId  Entity;
        Component Before{};
        bool      Active = false;
        bool      Finished = false;

        // Call immediately after the drag widget. ImGui's item queries name the
        // last item submitted, so both edges have to be read right there rather
        // than at the end of the component's rows. `before` must be the value as
        // of this frame's start: on the frame a widget activates it may already
        // have written.
        void Track(EntityId entity, const Component& before)
        {
            if (ImGui::IsItemActivated())
            {
                Entity = entity;
                Before = before;
                Active = true;
            }
            if (Active && Entity == entity && ImGui::IsItemDeactivatedAfterEdit())
            {
                Active = false;
                Finished = true;
            }
        }

        // The pre-gesture value, once a gesture has ended.
        [[nodiscard]] std::optional<Component> Ended()
        {
            if (!Finished)
                return std::nullopt;
            Finished = false;
            return Before;
        }
    };

    // A name the registry no longer knows, which authored content can outlive:
    // shown as itself rather than as a blank row.
    std::string DisplayName(std::string_view name)
    {
        return name.empty() ? std::string("<unregistered>") : std::string(name);
    }

    void DrawRefusal(const std::string& message)
    {
        if (message.empty())
            return;
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.25f, 1.0f), "%s", message.c_str());
    }

    // The narrow row button every list uses to drop an entry.
    bool DrawRemoveButton()
    {
        return ImGui::SmallButton(ICON_FA_XMARK);
    }

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

        bool DrawInspector(EditorComponentInspectorContext& context) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const GameplayTagContainer* held =
                std::as_const(world).TryGet<GameplayTagContainer>(context.Entity);
            if (held == nullptr)
                return false;

            auto* registry = world.TryGetResource<GameplayTagRegistry>();
            if (registry == nullptr)
            {
                ImGui::TextDisabled("This document has no gameplay tag registry.");
                return true;
            }

            if (StatusEntity != context.Entity)
            {
                StatusEntity = context.Entity;
                Status.clear();
                NewTag.fill('\0');
            }

            const GameplayTagContainer before = *held;
            GameplayTagContainer working = before;
            bool previewed = false;
            bool instant = false;

            for (std::uint8_t i = 0; i < before.Count; ++i)
            {
                const GameplayTagId tag = before.Tags[i];
                ImGui::PushID(static_cast<int>(tag.Value));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(DisplayName(registry->GetName(tag)).c_str());

                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
                int stacks = static_cast<int>(before.Counts[i]);
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragInt("##stacks", &stacks, 0.2f, 1, 999);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("How many sources grant this tag. It stays "
                                      "present until every one of them revokes it.");
                Drag.Track(context.Entity, before);
                const auto clamped = static_cast<std::uint16_t>(std::clamp(stacks, 1, 999));
                if (clamped != before.Counts[i])
                    previewed |= SetTagStacks(working, tag, clamped).Changed;

                ImGui::SameLine();
                if (DrawRemoveButton())
                    instant |= RevokeTag(working, tag).Changed;
                ImGui::PopID();
            }
            if (before.Count == 0)
                ImGui::TextDisabled("No tags.");

            // Open vocabulary: pick a registered name, or type a new dot-path
            // and it is declared here. The scene stores the name either way.
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
            const bool entered = ImGui::InputTextWithHint(
                "##newtag", "State.Stunned", NewTag.data(), NewTag.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::SmallButton("Add") || entered)
            {
                const VocabularyEdit edit =
                    GrantTagByName(working, *registry, NewTag.data());
                Status = edit.Error;
                instant |= edit.Changed;
                if (edit.Changed)
                    NewTag.fill('\0');
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_CARET_DOWN "##knowntags"))
                ImGui::OpenPopup("known_tags");
            if (ImGui::BeginPopup("known_tags"))
            {
                bool anyOffered = false;
                ForEachRegistered<GameplayTagId>(*registry, [&](GameplayTagId tag)
                {
                    if (working.HasExact(tag))
                        return;
                    anyOffered = true;
                    const std::string name(registry->GetName(tag));
                    if (ImGui::Selectable(name.c_str()))
                    {
                        const VocabularyEdit edit =
                            GrantTagByName(working, *registry, name);
                        Status = edit.Error;
                        instant |= edit.Changed;
                    }
                });
                if (!anyOffered)
                    ImGui::TextDisabled("Every registered tag is already here.");
                ImGui::EndPopup();
            }
            DrawRefusal(Status);

            if (previewed || instant)
                context.Scene.SetComponent(context.Entity, working);
            if (instant)
                Commit(context, before, working);
            else if (std::optional<GameplayTagContainer> pre = Drag.Ended())
                Commit(context, *pre, working);
            return true;
        }

    private:
        mutable DragGesture<GameplayTagContainer> Drag;
        mutable EntityId                          StatusEntity;
        mutable std::string                       Status;
        mutable std::array<char, 128>             NewTag{};
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

        bool DrawInspector(EditorComponentInspectorContext& context) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const AttributeSet* held =
                std::as_const(world).TryGet<AttributeSet>(context.Entity);
            if (held == nullptr)
                return false;

            auto* registry = world.TryGetResource<AttributeRegistry>();
            if (registry == nullptr)
            {
                ImGui::TextDisabled("This document has no attribute registry.");
                return true;
            }

            if (StatusEntity != context.Entity)
            {
                StatusEntity = context.Entity;
                Status.clear();
            }

            const AttributeSet before = *held;
            AttributeSet working = before;
            bool previewed = false;
            bool instant = false;

            for (std::uint8_t i = 0; i < before.Count; ++i)
            {
                const AttributeId id = before.Ids[i];
                ImGui::PushID(static_cast<int>(id.Value));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(DisplayName(registry->GetName(id)).c_str());

                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
                float base = before.Base[i];
                ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() * 2.0f));
                ImGui::DragFloat("##base", &base, 0.05f,
                                 registry->Min(id), registry->Max(id));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("The authored value. What the entity has in "
                                      "play is this folded with active effects.");
                Drag.Track(context.Entity, before);
                if (base != before.Base[i])
                    previewed |= SetAttributeBase(working, *registry, id, base).Changed;

                ImGui::SameLine();
                if (DrawRemoveButton())
                    instant |= RemoveAttribute(working, id).Changed;
                ImGui::PopID();
            }
            if (before.Count == 0)
                ImGui::TextDisabled("No attributes.");

            if (ImGui::SmallButton("Add attribute"))
                ImGui::OpenPopup("known_attributes");
            if (ImGui::BeginPopup("known_attributes"))
            {
                bool anyOffered = false;
                ForEachRegistered<AttributeId>(*registry, [&](AttributeId id)
                {
                    if (working.Has(id))
                        return;
                    anyOffered = true;
                    if (ImGui::Selectable(DisplayName(registry->GetName(id)).c_str()))
                    {
                        const VocabularyEdit edit = AddAttribute(working, *registry, id);
                        Status = edit.Error;
                        instant |= edit.Changed;
                    }
                });
                if (!anyOffered)
                    ImGui::TextDisabled(registry->Size() == 0
                                            ? "No attributes are registered here."
                                            : "Every registered attribute is already here.");
                ImGui::EndPopup();
            }
            DrawRefusal(Status);

            if (previewed || instant)
                context.Scene.SetComponent(context.Entity, working);
            if (instant)
                Commit(context, before, working);
            else if (std::optional<AttributeSet> pre = Drag.Ended())
                Commit(context, *pre, working);
            return true;
        }

    private:
        mutable DragGesture<AttributeSet> Drag;
        mutable EntityId                  StatusEntity;
        mutable std::string               Status;
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

        bool DrawInspector(EditorComponentInspectorContext& context) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const AbilitySet* held =
                std::as_const(world).TryGet<AbilitySet>(context.Entity);
            if (held == nullptr)
                return false;

            auto* registry = world.TryGetResource<AbilityRegistry>();
            if (registry == nullptr)
            {
                ImGui::TextDisabled("This document has no ability registry.");
                return true;
            }

            if (StatusEntity != context.Entity)
            {
                StatusEntity = context.Entity;
                Status.clear();
            }

            const AbilitySet before = *held;
            AbilitySet working = before;
            bool instant = false;

            for (std::uint8_t i = 0; i < before.Count; ++i)
            {
                const AbilityId id = before.Abilities[i];
                ImGui::PushID(static_cast<int>(id.Value));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(DisplayName(registry->GetName(id)).c_str());
                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
                if (DrawRemoveButton())
                    instant |= RevokeAbility(working, id).Changed;
                ImGui::PopID();
            }
            if (before.Count == 0)
                ImGui::TextDisabled("No abilities.");

            if (ImGui::SmallButton("Grant ability"))
                ImGui::OpenPopup("known_abilities");
            if (ImGui::BeginPopup("known_abilities"))
            {
                bool anyOffered = false;
                ForEachRegistered<AbilityId>(*registry, [&](AbilityId id)
                {
                    if (working.Has(id))
                        return;
                    anyOffered = true;
                    if (ImGui::Selectable(DisplayName(registry->GetName(id)).c_str()))
                    {
                        const VocabularyEdit edit = GrantAbility(working, *registry, id);
                        Status = edit.Error;
                        instant |= edit.Changed;
                    }
                });
                if (!anyOffered)
                    ImGui::TextDisabled(registry->Size() == 0
                                            ? "No abilities are registered here."
                                            : "Every registered ability is already here.");
                ImGui::EndPopup();
            }
            DrawRefusal(Status);

            if (instant)
            {
                context.Scene.SetComponent(context.Entity, working);
                Commit(context, before, working);
            }
            return true;
        }

    private:
        mutable EntityId    StatusEntity;
        mutable std::string Status;
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

        bool DrawInspector(EditorComponentInspectorContext& context) const override
        {
            World& world = context.Scene.GetRegistry().Components;
            const CharacterMovement* held =
                std::as_const(world).TryGet<CharacterMovement>(context.Entity);
            if (held == nullptr)
                return false;

            auto* modes = world.TryGetResource<LocomotionModeRegistry>();
            if (modes == nullptr)
            {
                ImGui::TextDisabled("This document has no locomotion mode registry.");
                return true;
            }

            const LocomotionModeEntry* current = modes->Find(held->Mode);
            const std::string preview = current != nullptr ? current->Name
                                                           : std::string("<unregistered>");

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Starting mode");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Which locomotion rules the character spawns under. "
                                  "Modes change during play; this is only where it begins.");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##mode", preview.c_str()))
            {
                for (const LocomotionModeEntry& mode : modes->Entries())
                {
                    const bool selected = mode.Id == held->Mode;
                    if (ImGui::Selectable(mode.Name.c_str(), selected) && !selected)
                    {
                        CharacterMovement after = *held;
                        if (SetLocomotionMode(after, *modes, mode.Name).Changed)
                        {
                            context.Scene.SetComponent(context.Entity, after);
                            Commit(context, *held, after);
                        }
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return true;
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
