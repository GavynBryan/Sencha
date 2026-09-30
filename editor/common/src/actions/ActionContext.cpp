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

const ActionHandler* ActionContextStack::Resolve(std::string_view id) const
{
    const Entry* best = nullptr;
    const ActionHandler* handler = nullptr;
    for (const Entry& entry : Entries)
    {
        const ActionHandler* found = entry.Context->Find(id);
        if (found == nullptr)
            continue;
        // Entries are in push order, so a later entry of equal rank is newer.
        if (best == nullptr || entry.Rank >= best->Rank)
        {
            best = &entry;
            handler = found;
        }
    }
    return handler;
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
    const ActionHandler* handler = Resolve(id);
    if (handler == nullptr)
        return false;
    if ((!handler->Enabled || handler->Enabled()) && handler->Invoke)
        handler->Invoke();
    return true;
}

bool ActionContextStack::Dispatch(const ActionCatalog& catalog, const KeyChord& chord) const
{
    const ActionDefinition* definition = catalog.ForChord(chord);
    return definition != nullptr && Invoke(definition->Id);
}

void ActionContextStack::Remove(std::uint64_t id)
{
    std::erase_if(Entries, [id](const Entry& entry) { return entry.Id == id; });
}
