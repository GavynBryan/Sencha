#include "authoring/AnimationRigProblems.h"

#include "data/DataDocumentStore.h"
#include "data/DataPath.h"
#include "text/PathLeaf.h"

#include <format>
#include <map>
#include <optional>

namespace
{
    class InheritedFrom
    {
    public:
        explicit InheritedFrom(DataDocumentStore& store) : Store(store) {}

        [[nodiscard]] std::string operator()(const std::string& asset, std::string_view field)
        {
            if (!field.starts_with(kDataPathRoot))
                return {};
            auto found = Reports.find(asset);
            if (found == Reports.end())
                found = Reports.emplace(asset, Store.CompositionOf(asset)).first;
            const CompositionOrigin* origin =
                found->second ? FindCompositionOrigin(found->second->Provenance,
                                                      "$" + std::string(field.substr(kDataPathRoot.size())))
                              : nullptr;
            return origin != nullptr && origin->Layer != CompositionLayer::Local ? origin->Asset : std::string{};
        }

    private:
        DataDocumentStore& Store;
        std::map<std::string, std::optional<CompositionReport>, std::less<>> Reports;
    };
}

std::vector<Problem> AnimationRigProblems(std::span<const AnimDiagnostic> diagnostics,
                                          std::span<const std::string> undeclared, DataDocumentStore& store)
{
    std::vector<Problem> problems;
    InheritedFrom inheritedFrom(store);
    for (const AnimDiagnostic& diagnostic : diagnostics)
    {
        std::string message = diagnostic.AssetPath.empty()
            ? diagnostic.Message
            : std::format("{}: {}", PathLeaf(diagnostic.AssetPath), diagnostic.Message);
        if (store.IsRegistered(diagnostic.AssetPath))
            if (const std::string source = inheritedFrom(diagnostic.AssetPath, diagnostic.FieldPath); !source.empty())
                message += std::format(" (inherited from '{}')", source);
        Problem problem{ .Severity = diagnostic.Severity == AnimDiagnosticSeverity::Error ? ProblemSeverity::Error
                                                                                          : ProblemSeverity::Warning,
                         .Message = std::move(message),
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
