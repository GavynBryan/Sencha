#pragma once

#include "script/ScriptModule.h"

#include <cstdint>
#include <string>

//=============================================================================
// ScriptBytecodeValidator (docs/plans/t-language-v1-spec.md, section A)
//
// Load-time re-check of compiler output: a structural pass (rules 1-6), then
// a type-flow pass (rules 7-10) as forward dataflow over each function's CFG.
// Runs at every module load; the compiler is not trusted.
//
// Type-flow softness, deliberate in bytecode v0: the constant pool carries
// raw u64s and the Functions section carries argument slot counts, not
// types. LDC results and non-callback argument slots therefore enter the
// flow as Unknown, which any typed operand position accepts. Callback
// functions are fully typed from their declaration kind's register prelude
// (spec section C), and every slot must still be defined before use, every
// join must agree on concrete types, and every width/range rule is exact.
//=============================================================================

struct ScriptValidationResult
{
    bool Ok = false;
    std::string Error;      // empty when Ok
    const char* Rule = "";  // e.g. "structural-2", "type-8"
    uint32_t FunctionIndex = 0;
    uint32_t CodeOffset = 0; // word offset within the function
};

[[nodiscard]] ScriptValidationResult ValidateScriptModule(const ScriptModule& module);
