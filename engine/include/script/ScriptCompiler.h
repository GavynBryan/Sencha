#pragma once

#include "script/ScriptModule.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

//=============================================================================
// ScriptCompiler (docs/plans/t-language-v1-spec.md, sections D, E milestone 3)
//
// Compiles T source to a bytecode-v0 ScriptModule. Pure: sources arrive
// through the resolver callback (the cook importer and tests supply file
// contents), errors are returned with file:line:col, nothing is logged.
// Every produced module must pass ScriptBytecodeValidator; the golden tests
// enforce that.
//
// The compiler knows engine-owned surfaces (host components like Transform,
// host records like RayHit, the host function table, host enums) through
// ScriptHostDecls. The default set transcribes spec section C; games extend
// it at cook configuration, not by editing the compiler.
//=============================================================================

struct ScriptCompileError
{
    std::string File;
    uint32_t Line = 0; // 1-based
    uint32_t Col = 0;  // 1-based
    std::string Message;
};

struct ScriptCompileResult
{
    bool Ok = false;
    ScriptCompileError Error;
    ScriptModule Module;
};

// Field of a host (native) component as scripts see it.
struct ScriptHostFieldDecl
{
    std::string_view Name;       // script-visible: "position"
    std::string_view EnginePath; // bind path: "local.position"
    ScriptScalarKind Scalar = ScriptScalarKind::Double;
    uint8_t SlotCount = 1;       // 3 for Vec3, and so on
};

struct ScriptHostComponentDecl
{
    std::string_view Name; // script-visible and bind name: "Transform"
    std::span<const ScriptHostFieldDecl> Fields;
};

struct ScriptHostDecls
{
    std::span<const ScriptHostComponentDecl> Components;
};

// Returns the spec section C surface: Transform plus the standard host
// functions and records, which are built into the compiler.
[[nodiscard]] const ScriptHostDecls& DefaultScriptHostDecls();

// Resolver contract: given a path relative to the importing file, yield the
// source text. Returning false fails the compile with an import error.
using ScriptSourceResolver =
    std::function<bool(std::string_view path, std::string& outText)>;

[[nodiscard]] ScriptCompileResult CompileScript(std::string_view mainPath,
                                                std::string_view mainSource,
                                                const ScriptSourceResolver& resolver,
                                                const ScriptHostDecls& hostDecls = DefaultScriptHostDecls());

// Renders a validated module as normative disassembly: one instruction per
// line with resolved names, stable across runs (feeds the golden tests and
// the script.disasm console command in a later milestone).
[[nodiscard]] std::string DisassembleScriptModule(const ScriptModule& module);
