#include "authoring/AbilityChecks.h"

#include "authoring/AbilityElements.h"
#include "data/DataPath.h"

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <ecs/World.h>

#include <format>

namespace
{
ProblemSeverity SeverityOf(AbilityDiagnosticSeverity severity)
{
    switch (severity)
    {
    case AbilityDiagnosticSeverity::Error: return ProblemSeverity::Error;
    case AbilityDiagnosticSeverity::Warning: return ProblemSeverity::Warning;
    case AbilityDiagnosticSeverity::Info: return ProblemSeverity::Info;
    }
    return ProblemSeverity::Error;
}

// "Charge › on release → Fire" for an element in a phase; the element alone outside one.
std::string WhereIn(const JsonValue& data, const AbilityElement& element)
{
    const std::string label = AbilityElementLabel(data, element);
    if (element.Phase.empty() || element.Kind == AbilityElementKind::Phase)
        return label;
    const std::optional<AbilityElement> phase = FindAbilityElement(data, element.Phase);
    return phase ? std::format("{} \xE2\x80\xBA {}", AbilityElementLabel(data, *phase), label) : label;
}
} // namespace

std::vector<AbilityDiagnostic> CheckAbilityData(const JsonValue& data, std::string_view name, const World& names)
{
    AbilityDeclaration compiled;
    return CheckAbilityData(data, name, names, compiled);
}

std::vector<AbilityDiagnostic> CheckAbilityData(const JsonValue& data, std::string_view name, const World& names,
                                                AbilityDeclaration& compiled, const AssetRegistry* assets)
{
    std::vector<AbilityDiagnostic> diagnostics;
    std::vector<AssetRef> dependencies;
    if (!CompileAbilityDeclaration(data, compiled, dependencies, diagnostics))
        return diagnostics;
    AbilityDefinition resolved;
    AbilityResolveEnvironment environment = MakeAbilityResolveEnvironment(names);
    // Only which assets exist: the working data's data assets are not loaded
    // through the cache, so their subtypes are left to the runtime's check.
    environment.Verbs.Assets = assets;
    (void)ResolveAbilityDeclaration(compiled, name, environment, resolved, diagnostics);
    return diagnostics;
}

std::vector<Problem> AbilityProblems(const std::vector<AbilityDiagnostic>& diagnostics, const JsonValue& data,
                                     const DocumentRef& document)
{
    std::vector<Problem> problems;
    problems.reserve(diagnostics.size());
    for (const AbilityDiagnostic& diagnostic : diagnostics)
    {
        std::optional<AbilityElement> element =
            diagnostic.ElementKey.empty() ? std::nullopt : FindAbilityElement(data, diagnostic.ElementKey);
        if (!element)
            element = AbilityElementAt(data, diagnostic.Path);
        const std::string message = element ? std::format("{}: {}", WhereIn(data, *element), diagnostic.Message)
                                            : diagnostic.Message;
        problems.push_back({ .Severity = SeverityOf(diagnostic.Severity),
                             .Message = message,
                             .Location = diagnostic.Path.empty() ? std::string(kDataPathRoot) : diagnostic.Path,
                             .Document = document });
    }
    return problems;
}
