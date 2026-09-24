#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

//=============================================================================
// CookDiagnostic
//
// A structured problem report from a cook step: what is wrong, how serious it
// is, and the stable identity of the authored record responsible, so tooling
// can take the author to the exact object instead of parsing a message.
// Errors fail the cook; warnings and info publish.
//=============================================================================

enum class CookDiagnosticSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

// What kind of authored record SourceId identifies.
enum class CookDiagnosticSource : std::uint8_t
{
    // The document or zone as a whole; SourceId is unused.
    Document,
    // The project's navigation settings asset; SourceId is unused.
    NavigationSettings,
    // An authored navigation link; SourceId is its NavLinkId value.
    NavLink,
};

struct CookDiagnostic
{
    CookDiagnosticSeverity Severity = CookDiagnosticSeverity::Error;
    CookDiagnosticSource Source = CookDiagnosticSource::Document;
    std::uint64_t SourceId = 0;
    // Stable dotted rule name, for example "nav.link.entry_unprojected".
    std::string Rule;
    std::string Message;
};

[[nodiscard]] inline bool HasCookErrors(std::span<const CookDiagnostic> diagnostics)
{
    return std::ranges::any_of(diagnostics, [](const CookDiagnostic& diagnostic)
                               { return diagnostic.Severity == CookDiagnosticSeverity::Error; });
}
