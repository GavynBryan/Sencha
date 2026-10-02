#include "authoring/AnimationRigProblems.h"

#include "data/DataDocumentStore.h"
#include "text/PathLeaf.h"

#include <format>

std::vector<Problem> AnimationRigProblems(std::span<const AnimDiagnostic> diagnostics,
                                          std::span<const std::string> undeclared, DataDocumentStore& store)
{
    std::vector<Problem> problems;
    for (const AnimDiagnostic& diagnostic : diagnostics)
    {
        Problem problem{ .Severity = diagnostic.Severity == AnimDiagnosticSeverity::Error ? ProblemSeverity::Error
                                                                                          : ProblemSeverity::Warning,
                         .Message = diagnostic.AssetPath.empty()
                                        ? diagnostic.Message
                                        : std::format("{}: {}", PathLeaf(diagnostic.AssetPath), diagnostic.Message),
                         .Location = diagnostic.FieldPath };
        if (store.IsRegistered(diagnostic.AssetPath))
            problem.Document = DocumentRef{ &store, diagnostic.AssetPath };
        problems.push_back(std::move(problem));
    }
    if (!undeclared.empty())
    {
        std::string names;
        for (const std::string& name : undeclared)
            names += (names.empty() ? "" : ", ") + name;
        problems.push_back({ .Severity = ProblemSeverity::Warning,
                             .Message = std::format("Not declared anywhere: {}. Declare them beside the rig.", names),
                             .Location = std::string(kUndeclaredAnimationNames) });
    }
    return problems;
}
