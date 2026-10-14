#include "authoring/AbilityProblemProperties.h"

#include "authoring/AbilityElements.h"
#include "authoring/AbilityGuardFixes.h"
#include "authoring/AbilityWorkbench.h"
#include "properties/SettingProperties.h"

#include <charconv>
#include <format>

namespace
{
constexpr std::string_view kProblem = "problem.";

std::string_view SeverityLabel(AbilityDiagnosticSeverity severity)
{
    switch (severity)
    {
    case AbilityDiagnosticSeverity::Error: return "Error";
    case AbilityDiagnosticSeverity::Warning: return "Warning";
    case AbilityDiagnosticSeverity::Info: return "Note";
    }
    return "Problem";
}

// Whether the diagnostic is about `element` or something inside it.
bool About(const AbilityDiagnostic& diagnostic, const AbilityElement& element)
{
    if (diagnostic.ElementKey == element.Key)
        return true;
    const std::string_view path = diagnostic.Path;
    return path.starts_with(element.Path)
        && (path.size() == element.Path.size() || path[element.Path.size()] == '.' || path[element.Path.size()] == '[');
}
} // namespace

std::vector<PropertyNode> AbilityProblemProperties::Nodes() const
{
    const std::optional<AbilityElement> element = Workbench.SelectedElement();
    const JsonValue* data = Workbench.AbilityData();
    if (!element || data == nullptr)
        return {};
    std::vector<PropertyNode> nodes;
    const std::vector<AbilityDiagnostic>& diagnostics = Workbench.Diagnostics();
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        if (!About(diagnostics[i], *element))
            continue;
        PropertyNode note = NoteProperty(std::format("{}{}", kProblem, i), std::string(SeverityLabel(diagnostics[i].Severity)),
                                         diagnostics[i].Message);
        if (const std::optional<AbilityGuardFix> fix = AbilityGuardFixFor(*data, diagnostics[i], Workbench.Names()))
            note.Descriptor.Verbs.push_back({ .Id = "fix", .Label = fix->Label });
        nodes.push_back(std::move(note));
    }
    return nodes;
}

PropertyEditResult AbilityProblemProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    std::size_t index = 0;
    const std::string_view number = key.starts_with(kProblem) ? key.substr(kProblem.size()) : std::string_view{};
    const std::vector<AbilityDiagnostic>& diagnostics = Workbench.Diagnostics();
    const JsonValue* data = Workbench.AbilityData();
    if (verb != "fix" || number.empty()
        || std::from_chars(number.data(), number.data() + number.size(), index).ec != std::errc{}
        || index >= diagnostics.size() || data == nullptr)
        return PropertyEditResult::Gone;
    const std::optional<AbilityGuardFix> fix = AbilityGuardFixFor(*data, diagnostics[index], Workbench.Names());
    if (!fix)
        return PropertyEditResult::Gone;
    return Workbench.Edit([&](JsonValue& edited) { return ApplyAbilityGuardFix(edited, *fix); })
             ? PropertyEditResult::Applied
             : PropertyEditResult::Refused;
}
