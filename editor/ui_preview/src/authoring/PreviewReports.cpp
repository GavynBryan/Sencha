#include "authoring/PreviewReports.h"

#include "authoring/UiPreviewModel.h"

#include <algorithm>
#include <format>

namespace
{
std::string_view KindLabel(UiDiagnosticKind kind)
{
    switch (kind)
    {
    case UiDiagnosticKind::UnsupportedStyle:     return "Unsupported style";
    case UiDiagnosticKind::PackageUnavailable:   return "Package unavailable";
    case UiDiagnosticKind::ResourceUnresolved:   return "Resource unresolved";
    case UiDiagnosticKind::ModelRefused:         return "Model refused";
    case UiDiagnosticKind::DocumentInvalid:      return "Document invalid";
    case UiDiagnosticKind::RebuildFailed:        return "Rebuild failed, the previous document is still up";
    case UiDiagnosticKind::BindingMissing:       return "Binding missing";
    case UiDiagnosticKind::MemberMissing:        return "Member missing";
    case UiDiagnosticKind::EventCallbackMissing: return "Action missing";
    case UiDiagnosticKind::DocumentEngineOther:  return "Document engine";
    }
    return "Document engine";
}

bool AboutTheModel(UiDiagnosticKind kind)
{
    return kind == UiDiagnosticKind::BindingMissing || kind == UiDiagnosticKind::MemberMissing
           || kind == UiDiagnosticKind::EventCallbackMissing;
}

std::string Location(const UiDiagnostic& diagnostic)
{
    if (AboutTheModel(diagnostic.Kind))
        return std::string(kPreviewModelLocation);
    if (!diagnostic.Path)
        return {};
    return diagnostic.Line ? std::format("{}:{}", *diagnostic.Path, *diagnostic.Line) : *diagnostic.Path;
}

std::string Argument(const UiValue& value)
{
    switch (value.Kind())
    {
    case UiValueKind::None:   return "none";
    case UiValueKind::Bool:   return value.AsBool() ? "true" : "false";
    case UiValueKind::Int:    return std::format("int {}", value.AsInt());
    case UiValueKind::Float:  return std::format("float {:.6g}", value.AsFloat());
    case UiValueKind::String: return std::format("\"{}\"", value.AsString());
    case UiValueKind::Id:     return std::format("id {}", value.AsId());
    }
    return "?";
}
} // namespace

std::vector<Problem> PreviewProblems(const std::deque<UiDiagnostic>& diagnostics, UiSurfaceId surface)
{
    std::vector<Problem> problems;
    for (const UiDiagnostic& diagnostic : diagnostics)
    {
        if (diagnostic.Severity == UiDiagnosticSeverity::Info)
            continue;
        if (diagnostic.Surface.IsValid() && diagnostic.Surface != surface)
            continue;
        Problem problem{
            .Severity = diagnostic.Severity == UiDiagnosticSeverity::Error ? ProblemSeverity::Error
                                                                           : ProblemSeverity::Warning,
            .Message = std::format("{}: {}", KindLabel(diagnostic.Kind), diagnostic.Message),
            .Location = Location(diagnostic),
        };
        const bool seen = std::ranges::any_of(problems, [&problem](const Problem& earlier) {
            return earlier.Severity == problem.Severity && earlier.Message == problem.Message
                   && earlier.Location == problem.Location;
        });
        if (!seen)
            problems.push_back(std::move(problem));
    }
    return problems;
}

std::string DescribeRaisedAction(const UiAction& action, const UiPreviewModel& model)
{
    const std::size_t declared = action.Id.Value;
    std::string text = declared >= 1 && declared <= model.Actions.size() ? model.Actions[declared - 1]
                                                                          : std::format("action {}", declared);
    if (action.Arguments.empty())
        return text;
    text += " (";
    for (std::size_t i = 0; i < action.Arguments.size(); ++i)
    {
        if (i > 0)
            text += ", ";
        text += Argument(action.Arguments[i]);
    }
    return text + ")";
}
