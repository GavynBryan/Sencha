#pragma once

#include <cstdint>

//=============================================================================
// ScriptImports
//
// A module's host imports resolved to small integers, computed once at link so
// the interpreter and the world host dispatch on an id instead of comparing
// import names on the per-callback hot path. Intrinsic imports resolve to an
// intrinsic id the VM executes inline; host imports resolve to a host op id the
// world host switches on.
//=============================================================================

enum class ScriptImportKind : std::uint8_t
{
    Intrinsic,
    Host,
};

struct ResolvedScriptImport
{
    ScriptImportKind Kind = ScriptImportKind::Host;
    // Intrinsic id (FindScriptIntrinsic) when Kind is Intrinsic; a host op id
    // (ScriptHostOp) when Kind is Host. -1 marks an import the host does not
    // implement, which traps as an argument error at call time.
    std::int32_t Id = -1;
};
