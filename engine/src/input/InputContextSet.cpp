#include <input/InputContextSet.h>

#include <cstring>

std::uint32_t InputContextSet::SlotFor(std::string_view name)
{
    const auto it = SlotsByName.find(std::string(name));
    if (it != SlotsByName.end())
        return it->second;

    const auto slot = static_cast<std::uint32_t>(Slots.size());
    Slots.push_back(Slot{ std::string(name), 0, false });
    SlotsByName.emplace(Slots.back().Name, slot);
    return slot;
}

InputContextLease InputContextSet::Activate(std::string_view name)
{
    const std::uint32_t slot = SlotFor(name);
    return InputContextLease(this, InputContextToken{ slot + 1 });
}

bool InputContextSet::IsActive(std::string_view name) const
{
    const auto it = SlotsByName.find(std::string(name));
    return it != SlotsByName.end() && Slots[it->second].Applied;
}

void InputContextSet::ApplyPending()
{
    for (Slot& slot : Slots)
        slot.Applied = slot.Leases > 0;
}

void InputContextSet::BuildActiveMask(const BoundInputProfile& profile,
                                      std::vector<std::uint8_t>& out) const
{
    out.assign(profile.Contexts.size(), 0);
    for (std::size_t i = 0; i < profile.Contexts.size(); ++i)
    {
        const auto it = SlotsByName.find(profile.Contexts[i].Name);
        if (it != SlotsByName.end() && Slots[it->second].Applied)
            out[i] = 1;
    }
}

void InputContextSet::Attach(std::uint64_t token)
{
    InputContextToken decoded{};
    std::memcpy(&decoded, &token, sizeof(decoded));
    if (decoded.Value == 0 || decoded.Value > Slots.size())
        return;
    ++Slots[decoded.Value - 1].Leases;
}

void InputContextSet::Detach(std::uint64_t token)
{
    InputContextToken decoded{};
    std::memcpy(&decoded, &token, sizeof(decoded));
    if (decoded.Value == 0 || decoded.Value > Slots.size())
        return;
    Slot& slot = Slots[decoded.Value - 1];
    if (slot.Leases > 0)
        --slot.Leases;
}
