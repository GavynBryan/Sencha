#pragma once

#include <script/ScriptModule.h>

#include <cstdint>
#include <string_view>

//=============================================================================
// ScriptCallbacks
//
// Resolves a declaration's named callbacks to function indices. Shared by the
// behavior and ability bridges, which both map a lifecycle name (spawn, start,
// finish, cancel) or the current state's fixed callback to the function the VM
// invokes. Pure module inspection: no world state.
//=============================================================================

// The function index of the callback named `name` in `decl`, or -1.
[[nodiscard]] std::int32_t FindScriptCallback(const ScriptModule& module,
                                              const ScriptDeclaration& decl,
                                              std::string_view name);

// The fixed-tick callback for the current state: a per-state "State.fixed" when
// in a state and one exists, otherwise the bare "fixed". -1 if neither exists.
[[nodiscard]] std::int32_t FindScriptFixedCallback(const ScriptModule& module,
                                                   const ScriptDeclaration& decl,
                                                   std::int32_t state);
