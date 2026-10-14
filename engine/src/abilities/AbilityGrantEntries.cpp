#include <abilities/AbilityGrantEntries.h>

#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <input/InputActionRegistry.h>

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

namespace
{
    template<typename T>
    T Pick(const std::optional<T>& override, const std::optional<T>& fallback, T otherwise)
    {
        return override ? *override : fallback ? *fallback : otherwise;
    }
}

GameplayTagQuery ResolveTagQuery(const AbilityTagQueryDecl& declared, const GameplayTagRegistry& tags,
                                 std::vector<std::string>& missing)
{
    GameplayTagQuery query;
    const auto add = [&](const std::vector<std::string>& names, auto addClause) {
        for (const std::string& name : names)
        {
            const GameplayTagId tag = tags.FindTag(name);
            if (!tag.IsValid())
            {
                missing.push_back(name);
                continue;
            }
            (query.*addClause)(tag, GameplayTagMatchMode::Hierarchical);
        }
    };
    add(declared.All, &GameplayTagQuery::AddAll);
    add(declared.Any, &GameplayTagQuery::AddAny);
    add(declared.None, &GameplayTagQuery::AddNone);
    return query;
}

std::uint16_t AbilityGrantEntries::Intern(AbilityGrantEntry entry)
{
    const auto found = std::ranges::find(Entries, entry, &Slot::Entry);
    if (found != Entries.end())
        return static_cast<std::uint16_t>(found - Entries.begin());
    if (Entries.size() >= kAbilityNoGrantEntry)
        return kAbilityNoGrantEntry;
    Entries.push_back(Slot{ std::move(entry), {} });
    Stale = true;
    return static_cast<std::uint16_t>(Entries.size() - 1);
}

const AbilityGrantEntry* AbilityGrantEntries::Find(std::uint16_t index) const
{
    return index < Entries.size() ? &Entries[index].Entry : nullptr;
}

const AbilityEffectiveBinding* AbilityGrantEntries::Binding(std::uint16_t index) const
{
    return index < Entries.size() ? &Entries[index].Binding : nullptr;
}

void AbilityGrantEntries::Refresh(const AbilityRegistry& abilities, const InputActionRegistry* actions,
                                  std::uint64_t actionsRevision, const GameplayTagRegistry& tags,
                                  std::vector<std::string>& problems)
{
    if (!Stale && AbilitiesGeneration == abilities.Generation() && Actions == actions
        && ActionsRevision == actionsRevision)
        return;
    Stale = false;
    AbilitiesGeneration = abilities.Generation();
    Actions = actions;
    ActionsRevision = actionsRevision;

    for (Slot& slot : Entries)
    {
        slot.Binding = AbilityEffectiveBinding{};
        const AbilityDefinition* definition = abilities.Get(slot.Entry.Ability);
        static const std::optional<AbilityBindingDecl> kNone;
        const std::optional<AbilityBindingDecl>& fallback = definition != nullptr ? definition->InputDefault : kNone;
        const AbilityBindingDecl& over = slot.Entry.Override;
        const auto field = [&](auto member) {
            return fallback ? (*fallback).*member : std::decay_t<decltype(over.*member)>{};
        };

        const std::optional<std::string> action = over.Action ? over.Action : field(&AbilityBindingDecl::Action);
        if (!action)
            continue;
        const InputActionId id = actions != nullptr ? actions->Find(*action) : InputActionId{};
        const std::size_t index = id.IsValid() ? InputActionRegistry::IndexOf(id) : kAbilityUnboundAction;
        if (index >= kAbilityUnboundAction)
        {
            if (actions != nullptr)
                problems.push_back(std::format("grant '{}' of '{}': no input action '{}'", slot.Entry.Key,
                                               abilities.GetName(slot.Entry.Ability), *action));
            continue;
        }
        slot.Binding.Action = static_cast<std::uint8_t>(index);
        slot.Binding.Policy = Pick(over.Policy, field(&AbilityBindingDecl::Policy), AbilityInputPolicy::Press);
        slot.Binding.Priority = Pick(over.Priority, field(&AbilityBindingDecl::Priority), 0);
        slot.Binding.Consume = Pick(over.Consume, field(&AbilityBindingDecl::Consume), AbilityConsume::Exclusive);
        slot.Binding.WhenBlocked =
            Pick(over.WhenBlocked, field(&AbilityBindingDecl::WhenBlocked), AbilityWhenBlocked::Consume);
        slot.Binding.OnInterrupt =
            Pick(over.OnInterrupt, field(&AbilityBindingDecl::OnInterrupt), AbilityInterrupt::Cancel);
        const std::optional<AbilityTagQueryDecl> context =
            over.Context ? over.Context : field(&AbilityBindingDecl::Context);
        if (context && !context->Empty())
        {
            std::vector<std::string> missing;
            slot.Binding.Context = ResolveTagQuery(*context, tags, missing);
            slot.Binding.HasContext = true;
            for (const std::string& name : missing)
                problems.push_back(std::format("grant '{}': no gameplay tag '{}'", slot.Entry.Key, name));
        }
    }
}
