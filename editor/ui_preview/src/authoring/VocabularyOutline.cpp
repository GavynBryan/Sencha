#include "authoring/VocabularyOutline.h"

#include <format>

namespace
{
constexpr std::string_view kBranchKey = "branch:";

std::string Arity(std::size_t count, std::string_view noun)
{
    return std::format("{} {}{}", count, noun, count == 1 ? "" : "s");
}

std::string Record(const VocabularyCatalog::BindingRow& record)
{
    const std::string_view status = !record.Resolved            ? "does not resolve"
                                    : !record.ReferencesChecked ? "references unchecked"
                                                                : "resolved";
    return std::format("{} > {} · {}", record.Key, record.VerbName, status);
}

std::vector<OutlineNode> Nodes(const VocabularyCatalog& catalog, std::span<const BindingAsset> bindings)
{
    std::vector<OutlineNode> nodes;
    const auto branch = [&nodes](std::string label, int depth = 0) {
        nodes.push_back({ .Key = std::string(kBranchKey) + label, .Label = label, .Depth = depth });
    };
    const auto entry = [&nodes](std::string label, int depth = 1) {
        nodes.push_back({ .Key = std::format("entry:{}", nodes.size()), .Label = std::move(label), .Depth = depth });
    };
    branch("Verbs");
    for (const VocabularyCatalog::VerbRow& verb : catalog.ListVerbs())
        entry(std::format("{} · {} · {} · {}", verb.Name, verb.DisplayName, verb.Provider,
                          Arity(verb.ArgumentCount, "argument")));
    branch("Queries");
    for (const VocabularyCatalog::QueryRow& query : catalog.ListQueries())
        entry(std::format("{} · {} · {} · {}", query.Name, query.DisplayName, query.Provider,
                          Arity(query.ArgumentCount, "argument")));
    branch("Events");
    for (const VocabularyCatalog::EventRow& event : catalog.ListEvents())
        entry(std::format("{} · {} · {} · {}", event.Name, event.SourceComponent, event.Provider,
                          Arity(event.PayloadCount, "field")));
    branch("Bindings");
    for (const BindingAsset& asset : bindings)
    {
        branch(asset.Path, 1);
        for (const VocabularyCatalog::BindingRow& record : asset.Records)
            entry(Record(record), 2);
    }
    return nodes;
}
} // namespace

ExplorerPage VocabularyOutline::Explore(const VocabularyCatalog& catalog, std::span<const BindingAsset> bindings,
                                        const ExplorerQuery& query)
{
    ExplorerPage page = Outline.Explore(Nodes(catalog, bindings), query);
    page.Actions = { "ui_preview.rescan" };
    page.Empty = "Nothing in the vocabulary holds that.";
    return page;
}

bool VocabularyOutline::Flip(std::string_view key)
{
    if (!key.starts_with(kBranchKey))
        return false;
    Outline.Flip(key);
    return true;
}

std::vector<Problem> VocabularyProblems(const VocabularyCatalog& catalog, std::span<const BindingAsset> bindings)
{
    std::vector<Problem> problems;
    for (const std::string& error : catalog.Errors())
        problems.push_back({ .Severity = ProblemSeverity::Error, .Message = "Vocabulary: " + error });
    for (const BindingAsset& asset : bindings)
        for (const VocabularyCatalog::BindingRow& record : asset.Records)
        {
            if (record.Resolved && record.ReferencesChecked)
                continue;
            problems.push_back({ .Severity = ProblemSeverity::Warning,
                                 .Message = record.Resolved
                                                ? std::format("{} resolves, but its asset references went unchecked",
                                                              record.Key)
                                                : std::format("{} does not resolve: {}", record.Key, record.Error),
                                 .Location = asset.Path });
        }
    return problems;
}
