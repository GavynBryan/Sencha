#include <abilities/AbilityGrantEntries.h>

#include <core/hash/Fnv1a.h>

#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <input/InputActionRegistry.h>

#include <algorithm>
#include <charconv>
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

    // Fixed field order and text forms: the key must match on every machine of one protocol.
    std::uint32_t EntryWireKey(const AbilityGrantEntry& entry)
    {
        constexpr char kField = '\x1f';
        constexpr char kItem = '\x1e';
        std::string text = entry.Key;
        const auto field = [&](char tag, const auto& value) {
            text += kField;
            text += tag;
            if (value)
                text += std::to_string(static_cast<std::int64_t>(*value));
        };
        const AbilityBindingDecl& binding = entry.Override;
        text += kField;
        text += 'a';
        if (binding.Action)
            text += *binding.Action;
        field('p', binding.Policy);
        field('r', binding.Priority);
        field('c', binding.Consume);
        field('w', binding.WhenBlocked);
        field('i', binding.OnInterrupt);
        text += kField;
        text += 'x';
        if (binding.Context)
            for (const std::vector<std::string>* names : { &binding.Context->All, &binding.Context->Any, &binding.Context->None })
            {
                text += kItem;
                for (const std::string& name : *names)
                    (text += name) += kItem;
            }
        if (!entry.Parameters.empty())
        {
            text += kField;
            text += 'v';
            char number[32];
            for (const AbilityGrantParameter& parameter : entry.Parameters)
            {
                const auto written = std::to_chars(number, number + sizeof(number), parameter.Value);
                ((text += kItem) += parameter.Key) += kItem;
                text.append(number, written.ptr);
            }
        }
        return NameWireKey(text);
    }

    std::uint64_t WireIndexKey(AbilityId ability, std::uint32_t wireKey)
    {
        return (std::uint64_t{ ability.Value } << 32) | wireKey;
    }
}

AbilityBindingDecl EffectiveAbilityBinding(const AbilityBindingDecl& over,
                                           const std::optional<AbilityBindingDecl>& fallback)
{
    const auto field = [&](auto member) {
        return fallback ? (*fallback).*member : std::decay_t<decltype(over.*member)>{};
    };
    AbilityBindingDecl merged;
    merged.Action = over.Action ? over.Action : field(&AbilityBindingDecl::Action);
    merged.Policy = Pick(over.Policy, field(&AbilityBindingDecl::Policy), AbilityInputPolicy::Press);
    merged.Priority = Pick(over.Priority, field(&AbilityBindingDecl::Priority), 0);
    merged.Consume = Pick(over.Consume, field(&AbilityBindingDecl::Consume), AbilityConsume::Exclusive);
    merged.WhenBlocked = Pick(over.WhenBlocked, field(&AbilityBindingDecl::WhenBlocked), AbilityWhenBlocked::Consume);
    merged.OnInterrupt = Pick(over.OnInterrupt, field(&AbilityBindingDecl::OnInterrupt), AbilityInterrupt::Cancel);
    merged.Context = over.Context ? over.Context : field(&AbilityBindingDecl::Context);
    return merged;
}

std::string_view AbilityGrantEntryRefusalText(AbilityGrantEntryRefusal refusal)
{
    switch (refusal)
    {
    case AbilityGrantEntryRefusal::Full: return "was refused: the grant entry table holds 65535 entries, the most it can";
    case AbilityGrantEntryRefusal::WireKeyTaken:
        return "was refused: another entry of its ability travels under the same wire key, so a peer could not tell "
               "them apart; rename one";
    }
    return "was refused";
}

std::uint16_t AbilityGrantEntries::Intern(AbilityGrantEntry entry, AbilityGrantEntryRefusal* refusal)
{
    const auto refuse = [&](AbilityGrantEntryRefusal why) {
        if (refusal != nullptr)
            *refusal = why;
        return kAbilityNoGrantEntry;
    };
    std::ranges::sort(entry.Parameters, {}, &AbilityGrantParameter::Key);
    const std::uint32_t key = EntryWireKey(entry);
    const auto [found, added] = ByWireKey.try_emplace(WireIndexKey(entry.Ability, key), kAbilityNoGrantEntry);
    if (!added)
        return Entries[found->second].Entry == entry ? found->second : refuse(AbilityGrantEntryRefusal::WireKeyTaken);
    if (Entries.size() >= kAbilityNoGrantEntry)
    {
        ByWireKey.erase(found);
        return refuse(AbilityGrantEntryRefusal::Full);
    }
    found->second = static_cast<std::uint16_t>(Entries.size());
    Entries.push_back(Slot{ std::move(entry), {}, {}, key });
    Stale = true;
    return found->second;
}

std::uint16_t AbilityGrantEntries::IndexOf(AbilityGrantEntry entry) const
{
    std::ranges::sort(entry.Parameters, {}, &AbilityGrantParameter::Key);
    const std::uint16_t index = FindByWireKey(EntryWireKey(entry), entry.Ability);
    return index != kAbilityNoGrantEntry && Entries[index].Entry == entry ? index : kAbilityNoGrantEntry;
}

const AbilityGrantEntry* AbilityGrantEntries::Find(std::uint16_t index) const
{
    return index < Entries.size() ? &Entries[index].Entry : nullptr;
}

std::uint16_t AbilityGrantEntries::FindByWireKey(std::uint32_t key, AbilityId ability) const
{
    const auto found = ByWireKey.find(WireIndexKey(ability, key));
    return found != ByWireKey.end() ? found->second : kAbilityNoGrantEntry;
}

std::uint32_t AbilityGrantEntries::WireKey(std::uint16_t index) const
{
    return index < Entries.size() ? Entries[index].WireKey : 0u;
}

const AbilityEffectiveBinding* AbilityGrantEntries::Binding(std::uint16_t index) const
{
    return index < Entries.size() ? &Entries[index].Binding : nullptr;
}

std::span<const double> AbilityGrantEntries::Parameters(std::uint16_t index) const
{
    return index < Entries.size() ? std::span<const double>(Entries[index].Parameters) : std::span<const double>{};
}

void AbilityGrantEntries::Refresh(const AbilityRegistry& abilities, const InputActionRegistry* actions,
                                  std::uint64_t actionsRevision, const GameplayTagRegistry& tags,
                                  std::vector<std::string>& problems)
{
    // Tags are only ever added, so the count is the vocabulary's revision.
    if (!Stale && AbilitiesGeneration == abilities.Generation() && Actions == actions
        && ActionsRevision == actionsRevision && Tags == &tags && TagCount == tags.Size())
        return;
    Stale = false;
    AbilitiesGeneration = abilities.Generation();
    Actions = actions;
    ActionsRevision = actionsRevision;
    Tags = &tags;
    TagCount = tags.Size();

    for (Slot& slot : Entries)
    {
        slot.Binding = AbilityEffectiveBinding{};
        const AbilityDefinition* definition = abilities.Get(slot.Entry.Ability);
        slot.Parameters.clear();
        if (definition != nullptr)
        {
            for (const AbilityParameterDecl& declared : definition->Parameters)
                slot.Parameters.push_back(declared.Default);
            for (const AbilityGrantParameter& parameter : slot.Entry.Parameters)
            {
                const auto found = std::ranges::find(definition->Parameters, parameter.Key, &AbilityParameterDecl::Key);
                if (found != definition->Parameters.end())
                    slot.Parameters[static_cast<std::size_t>(found - definition->Parameters.begin())] = parameter.Value;
                else
                    problems.push_back(std::format("grant '{}' of '{}': no parameter '{}'", slot.Entry.Key,
                                                   abilities.GetName(slot.Entry.Ability), parameter.Key));
            }
        }
        static const std::optional<AbilityBindingDecl> kNone;
        const AbilityBindingDecl merged = EffectiveAbilityBinding(
            slot.Entry.Override, definition != nullptr ? definition->InputDefault : kNone);
        slot.Binding.Priority = *merged.Priority;
        if (!merged.Action)
            continue;
        const InputActionId id = actions != nullptr ? actions->Find(*merged.Action) : InputActionId{};
        const std::size_t index = id.IsValid() ? InputActionRegistry::IndexOf(id) : kAbilityUnboundAction;
        if (index >= kAbilityUnboundAction)
        {
            if (actions != nullptr)
                problems.push_back(std::format("grant '{}' of '{}': no input action '{}'", slot.Entry.Key,
                                               abilities.GetName(slot.Entry.Ability), *merged.Action));
            continue;
        }
        const std::optional<AbilityTagQueryDecl>& context = merged.Context;
        if (context && !context->Empty())
        {
            std::vector<std::string> missing;
            GameplayTagQuery query = ResolveTagQuery(*context, tags, missing);
            for (const std::string& name : missing)
                problems.push_back(std::format("grant '{}': no gameplay tag '{}'", slot.Entry.Key, name));
            if (!missing.empty())
                continue;
            slot.Binding.Context = std::move(query);
            slot.Binding.HasContext = true;
        }
        slot.Binding.Action = static_cast<std::uint8_t>(index);
        slot.Binding.Policy = *merged.Policy;
        slot.Binding.Consume = *merged.Consume;
        slot.Binding.WhenBlocked = *merged.WhenBlocked;
        slot.Binding.OnInterrupt = *merged.OnInterrupt;
    }
}
