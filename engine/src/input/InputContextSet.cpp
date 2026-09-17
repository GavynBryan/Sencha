#include <input/InputContextSet.h>

#include <cstring>

namespace
{
    // Inverse of Owned<InputContextToken>::Encode(), which copies the token's
    // object representation into the low bytes of a zeroed uint64_t. Decoding
    // through a uint32_t rather than straight into InputContextToken keeps the
    // byte-level round trip identical while memcpy's destination stays a type
    // with a trivial default constructor.
    InputContextToken DecodeToken(std::uint64_t token)
    {
        std::uint32_t value = 0;
        std::memcpy(&value, &token, sizeof(value));
        return InputContextToken{ value };
    }
}

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
        // The shell's own context is not lease-managed. It is the
        // application's, always live, and exempt from the suspension it
        // applies -- a game able to deactivate it, or a suspension able to
        // silence it, would lock the player out of the menu with no way back.
        if (profile.Contexts[i].IsShell)
        {
            out[i] = 1;
            continue;
        }
        // Everything else goes quiet while the shell owns input. Lease state is
        // left exactly as it was, so lifting the suspension restores whatever
        // was active without anyone having to re-take it.
        if (Suspended)
            continue;
        const auto it = SlotsByName.find(profile.Contexts[i].Name);
        if (it != SlotsByName.end() && Slots[it->second].Applied)
            out[i] = 1;
    }
}

void InputContextSet::Attach(std::uint64_t token)
{
    const InputContextToken decoded = DecodeToken(token);
    if (decoded.Value == 0 || decoded.Value > Slots.size())
        return;
    ++Slots[decoded.Value - 1].Leases;
}

void InputContextSet::Detach(std::uint64_t token)
{
    const InputContextToken decoded = DecodeToken(token);
    if (decoded.Value == 0 || decoded.Value > Slots.size())
        return;
    Slot& slot = Slots[decoded.Value - 1];
    if (slot.Leases > 0)
        --slot.Leases;
}
