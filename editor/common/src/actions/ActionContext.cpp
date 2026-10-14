#include "actions/ActionContext.h"

#include "actions/ActionCatalog.h"

#include <algorithm>
#include <cassert>
#include <utility>

void ActionContext::Claim(std::string id, ActionBinding handler)
{
    Handlers[std::move(id)] = std::move(handler);
}

void ActionContext::Release(std::string_view id)
{
    if (const auto it = Handlers.find(id); it != Handlers.end())
        Handlers.erase(it);
}

const ActionBinding* ActionContext::Find(std::string_view id) const
{
    const auto it = Handlers.find(id);
    return it != Handlers.end() ? &it->second : nullptr;
}

ActionContextStack::Layer::Layer(Layer&& other) noexcept
    : Stack(std::exchange(other.Stack, nullptr))
    , Id(std::exchange(other.Id, 0))
{
}

ActionContextStack::Layer& ActionContextStack::Layer::operator=(Layer&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        Stack = std::exchange(other.Stack, nullptr);
        Id = std::exchange(other.Id, 0);
    }
    return *this;
}

void ActionContextStack::Layer::Reset()
{
    if (Stack != nullptr)
        Stack->Remove(Id);
    Stack = nullptr;
    Id = 0;
}

ActionContextStack::~ActionContextStack()
{
    assert(Entries.empty() && "a context outlived the window whose actions it answered");
}

ActionContextStack::Layer ActionContextStack::Push(const ActionContext& context, ActionRank rank)
{
    const std::uint64_t id = NextId++;
    Entries.push_back(Entry{ id, &context, rank });
    return Layer(*this, id);
}

bool ActionContextStack::Outranks(const Entry& entry, const Claim& other)
{
    return other.By == nullptr || entry.Rank > other.By->Rank
           || (entry.Rank == other.By->Rank && entry.Id > other.By->Id);
}

ActionContextStack::Claim ActionContextStack::Find(std::string_view id) const
{
    Claim best;
    for (const Entry& entry : Entries)
        if (const ActionBinding* found = entry.Context->Find(id); found != nullptr && Outranks(entry, best))
            best = Claim{ &entry, found };
    return best;
}

ActionContextStack::Claim ActionContextStack::FindEnabled(std::string_view id) const
{
    Claim best;
    for (const Entry& entry : Entries)
        if (const ActionBinding* found = entry.Context->Find(id);
            found != nullptr && found->IsEnabled() && Outranks(entry, best))
            best = Claim{ &entry, found };
    return best;
}

bool ActionContextStack::Perform(const Claim& claim)
{
    if (claim.Handler == nullptr)
        return false;
    (void)claim.Handler->TryInvoke();
    return true;
}

const ActionBinding* ActionContextStack::Resolve(std::string_view id) const
{
    return Find(id).Handler;
}

ActionContextStack::State ActionContextStack::Query(std::string_view id) const
{
    const ActionBinding* handler = Resolve(id);
    if (handler == nullptr)
        return {};
    return State{
        .Claimed = true,
        .Enabled = handler->IsEnabled(),
        .Checked = handler->Checked && handler->Checked(),
    };
}

bool ActionContextStack::Invoke(std::string_view id) const
{
    return Perform(Find(id));
}

bool ActionContextStack::Dispatch(const ActionCatalog& catalog, const KeyChord& chord) const
{
    Claim best;
    bool claimed = false;
    for (const ActionDefinition* definition : catalog.ForChord(chord))
    {
        claimed |= Find(definition->Id).By != nullptr;
        if (const Claim claim = FindEnabled(definition->Id); claim.By != nullptr && Outranks(*claim.By, best))
            best = claim;
    }
    (void)Perform(best);
    return claimed;
}

void ActionContextStack::Remove(std::uint64_t id)
{
    std::erase_if(Entries, [id](const Entry& entry) { return entry.Id == id; });
}
