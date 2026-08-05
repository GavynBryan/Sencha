#pragma once

#include <core/metadata/DataSchema.h>
#include <input/InputAction.h>
#include <input/InputBindings.h>

#include <cstdint>
#include <string>
#include <vector>

class DataAssetTypeRegistry;

//=============================================================================
// Authored input data
//
// Two .sdata subtypes, because they vary independently:
//
//   input.actions -- the action vocabulary, declared once per game.
//   input.profile -- controls bound to those actions, grouped into contexts.
//                    A game ships several of these (keyboard, gamepad, a
//                    player's rebinds) and every one names the same actions.
//
// Compiling a profile resolves control names, which depend on nothing else, and
// leaves action names as strings: the action set is a separate asset that may
// not be resident yet. Binding the two together is InputBindingCache's job, the
// same split MovementProfileBindingCache uses for tags.
//=============================================================================

inline constexpr std::string_view kInputActionSetTypeName = "input.actions";
inline constexpr std::string_view kInputProfileTypeName = "input.profile";

struct CompiledInputActionSet
{
    std::vector<InputActionDefinition> Actions;
};

// A binding as authored: the transforms and controls are already in their
// runtime form, only the action is still a name.
struct AuthoredInputBinding
{
    std::string Action;
    InputBinding Binding;
};

struct AuthoredInputContext
{
    std::string Name;
    std::int32_t Priority = 0;
    std::vector<AuthoredInputBinding> Bindings;
};

struct CompiledInputProfile
{
    // Virtual path of the input.actions asset these bindings name.
    std::string ActionSetPath;
    std::vector<AuthoredInputContext> Contexts;
};

// Registers both subtypes and their schemas. A schema that fails to register
// rolls its type back, so a subtype is never loadable without its validation.
void RegisterInputProfileData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
void UnregisterInputProfileData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);
