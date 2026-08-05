#pragma once

#include <input/InputAction.h>

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
// Ids are registration-order and one-based, so id.Value - 1 indexes the dense
// value arrays a resolve pass fills. They are runtime values: persist and
// transmit names, never ids.
//=============================================================================

class InputActionRegistry
{
public:
    // Rejects a duplicate name -- two definitions of one action mean two
    // different sets of bindings silently claim the same intent.
    bool Register(InputActionDefinition definition, std::string* error = nullptr);

    [[nodiscard]] InputActionId Find(std::string_view name) const;
    [[nodiscard]] const InputActionDefinition* Get(InputActionId id) const;
    [[nodiscard]] std::size_t Count() const { return Definitions.size(); }
    [[nodiscard]] std::span<const InputActionDefinition> Entries() const { return Definitions; }

    // Dense index for value arrays. Only valid for an id this registry minted.
    [[nodiscard]] static std::size_t IndexOf(InputActionId id) { return id.Value - 1; }

private:
    std::vector<InputActionDefinition> Definitions;
    std::unordered_map<std::string, InputActionId> IdsByName;
};
