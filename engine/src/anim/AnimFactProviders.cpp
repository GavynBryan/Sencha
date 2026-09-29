#include <anim/AnimFactProviders.h>

#include <algorithm>
#include <utility>

bool AnimFactProviders::Bind(std::string slot, AnimFactKind kind, AnimFactReadFn read,
                             const void* context)
{
    if (slot.empty() || read == nullptr || IndexOf(slot) >= 0)
        return false;
    Providers.push_back(AnimFactProvider{ std::move(slot), kind, read, context });
    ++Revision_;
    return true;
}

bool AnimFactProviders::Unbind(std::string_view slot)
{
    const auto it = std::find_if(Providers.begin(), Providers.end(),
                                 [slot](const AnimFactProvider& p) { return p.Slot == slot; });
    if (it == Providers.end())
        return false;
    Providers.erase(it);
    ++Revision_;
    return true;
}

int AnimFactProviders::IndexOf(std::string_view slot) const
{
    for (std::size_t i = 0; i < Providers.size(); ++i)
    {
        if (Providers[i].Slot == slot)
            return static_cast<int>(i);
    }
    return -1;
}
