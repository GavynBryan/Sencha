#include <anim/AnimDiagnostic.h>

#include <algorithm>
#include <format>

bool HasAnimErrors(const std::vector<AnimDiagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const AnimDiagnostic& d) {
        return d.Severity == AnimDiagnosticSeverity::Error;
    });
}

std::string FormatAnimDiagnostic(const AnimDiagnostic& diagnostic)
{
    return std::format("{} {}: {} [{}]", diagnostic.AssetPath,
                       diagnostic.FieldPath.empty() ? std::string("$") : diagnostic.FieldPath,
                       diagnostic.Message, diagnostic.Code);
}
