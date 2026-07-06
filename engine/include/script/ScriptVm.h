#pragma once

#include "script/ScriptModule.h"
#include "script/ScriptTrap.h"

#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// ScriptVm (docs/plans/t-language-v1-spec.md, sections A, C, D)
//
// The interpreter. Executes one callback invocation over a validated module:
// the VM trusts ScriptBytecodeValidator and carries no runtime type tags.
// Slots are untagged 64 bits.
//
// The VM holds no state between invocations (design principle 15): pending
// state transitions surface in the invocation result, persistent data lives
// in components behind the host handler, and the slot arena is scratch.
// Everything that touches engine state (components, tags, host calls above
// the intrinsic range) goes through ScriptHostCallHandler; Milestone 5
// implements it over the real seams, tests fake it.
//=============================================================================

class ScriptHostCallHandler
{
public:
    virtual ~ScriptHostCallHandler() = default;

    // Host imports at or above kScriptIntrinsicImportLimit. Arguments arrive
    // in window[0..argCount); the return value is written at the window base.
    virtual ScriptTrapCode HostCall(const ScriptModule& module, uint32_t importIndex,
                                    std::span<uint64_t> window, uint32_t argCount) = 0;

    // CLD/CST and their indexed forms. `elementIndex` is 0 for the direct
    // forms; the handler owns the array bound check for the indexed forms
    // (the bound lives in the component schema).
    virtual ScriptTrapCode ComponentLoad(const ScriptModule& module, uint64_t entityBits,
                                         uint32_t fieldBind, uint32_t elementIndex,
                                         std::span<uint64_t> out) = 0;
    virtual ScriptTrapCode ComponentStore(const ScriptModule& module, uint64_t entityBits,
                                          uint32_t fieldBind, uint32_t elementIndex,
                                          std::span<const uint64_t> in) = 0;
    virtual ScriptTrapCode ComponentHas(const ScriptModule& module, uint64_t entityBits,
                                        uint32_t componentBind, bool& out) = 0;

    virtual ScriptTrapCode TagHas(const ScriptModule& module, uint64_t entityBits,
                                  uint32_t tagBind, bool hierarchical, bool& out) = 0;
    virtual ScriptTrapCode TagAdd(const ScriptModule& module, uint64_t entityBits,
                                  uint32_t tagBind) = 0;
    virtual ScriptTrapCode TagRemove(const ScriptModule& module, uint64_t entityBits,
                                     uint32_t tagBind) = 0;
};

struct ScriptInvokeOptions
{
    // Per-callback instruction budget (cvar script.budget at the call site).
    uint64_t Budget = 100000;
    // Callback prelude / function arguments, copied to slots 0..N-1.
    std::span<const uint64_t> Args;
    // Destination for a RETV value; must match the function's RetSlots.
    std::span<uint64_t> Return;
};

struct ScriptInvokeResult
{
    ScriptTrapCode Trap = ScriptTrapCode::None;
    uint32_t TrapFunction = 0;   // function index, valid when trapped
    uint32_t TrapCodeOffset = 0; // word offset within that function
    // Last ENTER executed, as a state index into the owning declaration's
    // state list; -1 when no transition is pending (spec D.4).
    int32_t PendingState = -1;
    uint64_t InstructionsExecuted = 0;

    [[nodiscard]] bool Ok() const { return Trap == ScriptTrapCode::None; }
};

class ScriptVm
{
public:
    // `functionIndex` must be valid and the module validated; both are
    // asserted, not re-checked.
    [[nodiscard]] ScriptInvokeResult Invoke(const ScriptModule& module, uint32_t functionIndex,
                                            ScriptHostCallHandler& host,
                                            const ScriptInvokeOptions& options);

private:
    // Scratch arena reused across invocations, sized frames x slots.
    std::vector<uint64_t> Slots;
};
