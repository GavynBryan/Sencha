#pragma once

#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// AnimDiagnostic
//
// One problem with animation content, located where an author can fix it: the
// asset, the field path inside it, a stable code a tool can filter or offer a
// fix for, and a message that says which invariant was violated. The same
// record serves the runtime's bind step, cook validation and the editor's
// Problems view, so an error means the same thing wherever it surfaces.
//=============================================================================
enum class AnimDiagnosticSeverity : unsigned char
{
    Error,
    Warning,
};

struct AnimDiagnostic
{
    AnimDiagnosticSeverity Severity = AnimDiagnosticSeverity::Error;
    // "anim.fact.unknown_operand", "anim.rig.layer_unresolved", ...
    std::string Code;
    std::string AssetPath;
    // A JSON path into the asset's data, "$.data.derived[2].source".
    std::string FieldPath;
    std::string Message;
};

[[nodiscard]] bool HasAnimErrors(const std::vector<AnimDiagnostic>& diagnostics);

// "asset://x.sdata $.data.slots[1].name: message [code]" -- for logs and tests.
[[nodiscard]] std::string FormatAnimDiagnostic(const AnimDiagnostic& diagnostic);
