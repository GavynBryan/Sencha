#pragma once

#include <script/ScriptRuntime.h>

#include <string>

class World;

//=============================================================================
// ScriptLink
//
// Resolves a validated, world-agnostic module against a specific World and
// installs it into the World's ScriptRuntime resource. This is where the
// module's script-defined components register (RegisterComponentRaw, before
// any entity exists) and its symbolic binds resolve:
//   - tag names   -> GameplayTagId via the GameplayTagRegistry resource,
//   - component + field paths -> ComponentId + byte offset + scalar, from the
//     module's own schema (script components) or the native host-component
//     table (Transform),
//   - component binds -> ComponentId.
//
// Link failure names the missing symbol. Host imports and host-enum fixups
// are validated by the VM/host at call time in v1.0; behavior scripts that
// stay within components, tags, math, and cues link fully here.
//=============================================================================

struct ScriptLinkResult
{
    bool Ok = false;
    std::string Error;
    std::uint32_t ModuleIndex = 0; // index into ScriptRuntime.Modules when Ok
};

// Links `module` into `world` (creating the ScriptRuntime resource on first
// use). `module` must be validated. Must run before the first entity is
// created if the module declares components.
[[nodiscard]] ScriptLinkResult LinkScriptModule(World& world,
                                                std::shared_ptr<const ScriptModule> module);
