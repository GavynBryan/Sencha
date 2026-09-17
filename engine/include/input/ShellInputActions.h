#pragma once

#include <input/InputAction.h>
#include <input/InputActionRegistry.h>
#include <input/InputBindings.h>

#include <cstdint>
#include <span>
#include <string_view>

//=============================================================================
// The application shell's input vocabulary
//
// Backing out of gameplay is an operation every Sencha application has, not a
// thing each game invents: a player should not have to be told which key their
// game chose for "close this and show me the menu" any more than which corner
// closes the window. So the engine declares these actions and their default
// bindings, and merges them into whatever profile a game binds -- including
// into no profile at all, which is what lets a game shipping no input content
// still have a working menu.
//
// They are ordinary mapped actions, not a side channel. A profile that
// declares `ui.back` itself replaces the default binding, so remapping,
// controller profiles and accessibility settings keep working, and nothing in
// the engine reads a scancode.
//
// `ui.` is reserved. An action set may redeclare one of these and only these,
// with a matching type and scope; any other `ui.`-prefixed name fails the
// asset rather than quietly creating a second action nothing drives.
//=============================================================================

inline constexpr std::string_view kShellActionPrefix = "ui.";

// The context these resolve in. Its bindings survive the input suspension the
// shell applies while it owns input -- otherwise pausing would silence the very
// action that resumes.
inline constexpr std::string_view kShellContextName = "ui";

// Above any authored priority, so the shell's controls are never shadowed by a
// gameplay binding on the same key. An authored context at or above this is
// refused: the reserved band is the engine's.
inline constexpr std::int32_t kShellContextPriority = 1'000'000;

// Positional, and the order is the identity: the engine registers these first
// so their dense ids are the same in every profile and a host can hold them as
// constants rather than resolving names.
enum class ShellAction : std::uint8_t
{
    Back = 0,   // out of gameplay, out of a page, out of a field
    Accept,     // activate what has focus
    Up,
    Down,
    Left,
    Right,
    Count,
};

[[nodiscard]] std::string_view ShellActionName(ShellAction action);

// Null when the name is not one of these, which is how the reserved-namespace
// check tells a redeclaration from a mistake.
[[nodiscard]] const InputActionDefinition* FindShellAction(std::string_view name);

// Declared first in every compiled action set, so ShellAction is the dense id.
[[nodiscard]] std::span<const InputActionDefinition> ShellActionDefinitions();

// What each shell action is bound to when a profile says nothing about it.
// Dropped per action the moment an authored context binds that action, because
// a player who rebinds Back must not find the old key still working.
[[nodiscard]] std::span<const InputBinding> ShellDefaultBindings();

// The tables for a process with no authored profile at all -- a game shipping
// no input content, which templates/blank is. The shell's actions still resolve
// against their default bindings, which is what makes a working menu something
// a game has rather than something it builds.
void BuildShellOnlyProfile(InputActionRegistry& actions, BoundInputProfile& out);
