#pragma once

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <optional>
#include <string>

//=============================================================================
// UiDiagnostic
//
// One thing the authored UI layer noticed and a host might act on: a cooker's
// note about a stylesheet construct the renderer does not draw, this layer
// refusing to open a screen, the document engine failing to resolve a binding.
//
// Two orthogonal facts per entry. Source says where it came from; Kind says
// what happened. Attribution fields are optional and filled only when the
// layer genuinely knows them: a cook note knows its file and line, a refusal
// knows its package path, a binding miss knows the variable it named and the
// surface that was updating -- and the screen only when that surface had one.
// Nothing here is inferred from message text except the variable a document
// engine message names, which the bridge that owns that engine's wording
// extracts, under a test pinned to the vendored version.
//=============================================================================

enum class UiDiagnosticSeverity : std::uint8_t
{
    Info,
    Warning,
    Error,
};

enum class UiDiagnosticSource : std::uint8_t
{
    // A note the cooker recorded in the package, surfaced when a screen opens.
    Cook,
    // This layer's own refusal or failure.
    Runtime,
    // The document engine's log, attributed to what was loading or updating.
    DocumentEngine,
};

enum class UiDiagnosticKind : std::uint8_t
{
    // Cook: an RCSS construct outside the supported rendering profile. Path, Line, Screen.
    UnsupportedStyle,
    // Runtime: OpenScreen found no such package, or it was not resident. Path.
    PackageUnavailable,
    // Runtime: a font or image the package's table names could not be acquired. Path.
    ResourceUnresolved,
    // Runtime: a property, list, row-list or action name the model refused. Screen.
    ModelRefused,
    // Runtime: the package's root markup did not parse into a document. Path, Screen.
    DocumentInvalid,
    // Runtime: a reload could not rebuild the screen; the old document stays up. Path, Screen.
    RebuildFailed,
    // DocumentEngine: a data variable the model lacks. Variable is the name the
    // document used -- `title`, or a path through a list, `rows[0].label`.
    BindingMissing,
    // DocumentEngine: a struct member a row shape lacks, reported by the member's
    // bare name. Variable is that name; which list it was read through is not
    // said, and a BindingMissing naming the full path usually accompanies it.
    MemberMissing,
    // DocumentEngine: an undeclared action, reported when its event fires. Variable.
    EventCallbackMissing,
    // DocumentEngine: anything else it logged.
    DocumentEngineOther,
};

struct UiDiagnostic
{
    // Monotonic across the layer's lifetime. A gap between two drained entries
    // means the ring overflowed and the ones between were dropped.
    std::uint64_t Sequence = 0;
    UiDiagnosticSeverity Severity = UiDiagnosticSeverity::Info;
    UiDiagnosticSource Source = UiDiagnosticSource::Runtime;
    UiDiagnosticKind Kind = UiDiagnosticKind::DocumentEngineOther;
    std::string Message;

    // The screen being opened, rebuilt or restyled -- or, during a surface's
    // update, the one screen that surface carries. Invalid when the surface had
    // several and the engine did not say which.
    UiScreenHandle Screen;
    // Set whenever Screen is, and alone during a surface's update.
    UiSurfaceId Surface;
    // Cook notes and runtime refusals only.
    std::optional<std::string> Path;
    // Cook notes only.
    std::optional<std::uint32_t> Line;
    // BindingMissing and EventCallbackMissing only: the name the document used.
    std::optional<std::string> Variable;
};
