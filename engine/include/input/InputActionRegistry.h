#pragma once

#include <input/InputAction.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// InputActionRegistry
//
// The authoritative action list for a world, compiled from one authored action
// set. Adding an action is an edit to that document; every consumer references
// it by name once at startup and by dense id thereafter.
//
// Ids are dense and one-based, so id.Value - 1 indexes the dense value arrays a
// resolve pass fills. They are runtime values: persist and transmit names,
// never ids.
//
// A name keeps its id across every rebuild of this registry. Consumers resolve
// names once and cache the result, so an author reordering the action set mid
// session must not repoint a cached id at a different action -- the failure
// that would cause is silent and reads as the wrong control moving. A name the
// set stops declaring retires its slot instead of freeing it for reuse: the
// cached id then resolves to nothing, which is the truth, rather than to
// whichever action was authored next.
//=============================================================================

class InputActionRegistry
{
public:
    // Recompiles the vocabulary, keeping the id every name already had.
    //
    // All or nothing: a duplicate name makes the whole vocabulary ambiguous,
    // and a half-applied set would hand out ids for actions the author never
    // successfully declared. On failure the registry is untouched and the
    // caller still holds a usable previous vocabulary.
    bool Rebuild(std::span<const InputActionDefinition> definitions, std::string* error = nullptr);

    // The id for a name the current set declares. A retired name reads as
    // absent, the same as one that was never declared.
    [[nodiscard]] InputActionId Find(std::string_view name) const;
    [[nodiscard]] const InputActionDefinition* Get(InputActionId id) const;
    [[nodiscard]] bool IsLive(InputActionId id) const;

    // Slots ever minted, retired ones included. Dense value arrays are sized to
    // this, so a retired slot costs one always-zero value until the world ends.
    [[nodiscard]] std::size_t SlotCount() const { return Slots.size(); }

    // Dense index for value arrays. Only valid for an id this registry minted.
    [[nodiscard]] static std::size_t IndexOf(InputActionId id) { return id.Value - 1; }

private:
    enum class SlotState : std::uint8_t
    {
        Live,      // the current action set declares this name
        Retired,   // it no longer does; the slot stays, resolving to nothing
    };

    struct Slot
    {
        InputActionDefinition Definition;
        SlotState State = SlotState::Live;
    };

    std::vector<Slot> Slots;
    // Every name ever declared, retired ones included, so a name that comes
    // back gets the id it had before rather than a second slot.
    std::unordered_map<std::string, InputActionId> IdsByName;
};
