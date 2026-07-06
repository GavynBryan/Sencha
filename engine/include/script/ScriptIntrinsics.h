#pragma once

#include "script/ScriptTrap.h"

#include <cstdint>
#include <string_view>

//=============================================================================
// ScriptIntrinsics
//
// The core math imports, versioned with the VM and always present. The
// transcendentals use fixed polynomial kernels evaluated in a fixed order
// with only IEEE-exact primitive ops (+, -, *, fmod, floor, sqrt), so
// results are bit-identical across platforms and compilers. Accuracy is
// gameplay-grade (~1e-15 relative for arguments of gameplay magnitude), not
// correctly-rounded libm.
//=============================================================================

// Imports below this index are intrinsic; the VM executes them without the
// host handler.
inline constexpr uint32_t kScriptIntrinsicImportLimit = 32;

// Returns the intrinsic id for a dotted import name ("core.sin"), or -1 if
// the name is not an intrinsic.
[[nodiscard]] int32_t FindScriptIntrinsic(std::string_view dottedName);

// Executes intrinsic `id` over the call window (args in, return out at the
// window base). The window is at least as wide as the signature requires;
// the validator enforced that before execution.
[[nodiscard]] ScriptTrapCode ExecuteScriptIntrinsic(int32_t id, uint64_t* window);
