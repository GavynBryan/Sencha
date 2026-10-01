#include "actions/ActionContext.h"

#include "actions/ActionCatalog.h"

#include <algorithm>
#include <cassert>
#include <utility>

void ActionContext::Claim(std::string id, ActionHandler handler)
{
    Handlers[std::move(id)] = std::move(handler);
}

void ActionContext::Release(std::string_view id)
{
    Handlers.erase(std::string(id));
}

const ActionHandler* ActionContext::Find(std::string_view id) const
{
    const auto it = Handlers.find(std::string(id));
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
        if (const ActionHandler* found = entry.Context->Find(id); found != nullptr && Outranks(entry, best))
            best = Claim{ &entry, found };
    return best;
}

bool ActionContextStack::Perform(const Claim& claim)
{
    if (claim.Handler == nullptr)
        return false;
    if ((!claim.Handler->Enabled || claim.Handler->Enabled()) && claim.Handler->Invoke)
        claim.Handler->Invoke();
    return true;
}

const ActionHandler* ActionContextStack::Resolve(std::string_view id) const
{
    return Find(id).Handler;
}

ActionContextStack::State ActionContextStack::Query(std::string_view id) const
{
    const ActionHandler* handler = Resolve(id);
    if (handler == nullptr)
        return {};
    return State{
        .Claimed = true,
        .Enabled = !handler->Enabled || handler->Enabled(),
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
    for (const ActionDefinition* definition : catalog.ForChord(chord))
        if (const Claim claim = Find(definition->Id); claim.By != nullptr && Outranks(*claim.By, best))
            best = claim;
    return Perform(best);
}

void ActionContextStack::Remove(std::uint64_t id)
{
    std::erase_if(Entries, [id](const Entry& entry) { return entry.Id == id; });
}
