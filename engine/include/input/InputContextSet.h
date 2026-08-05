#pragma once

#include <core/handle/ILifetimeOwner.h>
#include <core/handle/Owned.h>
#include <input/InputBindings.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// InputContextSet
//
// Which contexts are live. Activation hands back a lease rather than taking a
// name to switch off later: a menu that is torn down, a vehicle the player
// leaves, or a dialogue interrupted by a load cannot leave its context stuck on,
// because dropping the lease is what deactivates it.
//
// Leases are counted, so two systems can hold the same context and it stays
// active until both let go.
//
// Contexts are addressed by name, not by index into the current profile: a
// profile that reloads with different contexts must not silently turn a held
// lease into a lease on some other context.
//=============================================================================

struct InputContextToken
{
    // Slot index plus one, so a default-constructed token stays invalid.
    std::uint32_t Value = 0;

    friend bool operator==(InputContextToken, InputContextToken) = default;
};

using InputContextLease = Owned<InputContextToken>;

class InputContextSet final : public ILifetimeOwner
{
public:
    // Hold this to keep a context active. Naming a context that no loaded
    // profile defines is not an error here: the lease is still counted, and the
    // context becomes live if a profile that defines it is loaded later.
    [[nodiscard]] InputContextLease Activate(std::string_view name);

    // Whether the context is active as of the last frame boundary. Reports the
    // applied state rather than the pending one, so a caller sees the same
    // answer the resolve pass used.
    [[nodiscard]] bool IsActive(std::string_view name) const;

    // Fold pending activations into the applied state. Called once per rendered
    // frame, before any tick, so every tick of one frame resolves against the
    // same set and a context taken mid-tick cannot change the tick count's
    // meaning.
    void ApplyPending();

    // Applied state indexed by the profile's context order, which is what a
    // resolve pass walks. Rebuilt per frame: a profile has a handful of
    // contexts, and caching it against a profile that rebinds in place would
    // need a validity signal more fragile than the lookup it saves.
    void BuildActiveMask(const BoundInputProfile& profile, std::vector<std::uint8_t>& out) const;

    [[nodiscard]] std::size_t Count() const { return Slots.size(); }

    void Attach(std::uint64_t token) override;
    void Detach(std::uint64_t token) override;

private:
    struct Slot
    {
        std::string Name;
        std::uint32_t Leases = 0;
        bool Applied = false;
    };

    std::uint32_t SlotFor(std::string_view name);

    std::vector<Slot> Slots;
    std::unordered_map<std::string, std::uint32_t> SlotsByName;
};
