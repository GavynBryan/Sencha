#include "authoring/VocabularyOutline.h"

#include "text/TextFilterMatch.h"

#include <format>

namespace
{
constexpr std::string_view kBranchKey = "branch:";

struct Entry
{
    std::string Label;
    int Depth = 1;
    // A branch's key, or empty for an entry.
    std::string Branch = {};
};

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

std::vector<Entry> Entries(const VocabularyCatalog& catalog, std::span<const BindingAsset> bindings)
{
    std::vector<Entry> entries;
    const auto branch = [&entries](std::string label, int depth = 0) {
        entries.push_back({ label, depth, std::string(kBranchKey) + label });
    };
    branch("Verbs");
    for (const VocabularyCatalog::VerbRow& verb : catalog.ListVerbs())
        entries.push_back({ std::format("{} · {} · {} · {}", verb.Name, verb.DisplayName, verb.Provider,
                                        Arity(verb.ArgumentCount, "argument")) });
    branch("Queries");
    for (const VocabularyCatalog::QueryRow& query : catalog.ListQueries())
        entries.push_back({ std::format("{} · {} · {} · {}", query.Name, query.DisplayName, query.Provider,
                                        Arity(query.ArgumentCount, "argument")) });
    branch("Events");
    for (const VocabularyCatalog::EventRow& event : catalog.ListEvents())
        entries.push_back({ std::format("{} · {} · {} · {}", event.Name, event.SourceComponent, event.Provider,
                                        Arity(event.PayloadCount, "field")) });
    branch("Bindings");
    for (const BindingAsset& asset : bindings)
    {
        branch(asset.Path, 1);
        for (const VocabularyCatalog::BindingRow& record : asset.Records)
            entries.push_back({ Record(record), 2 });
    }
    return entries;
}
} // namespace

ExplorerPage VocabularyOutline::Explore(const VocabularyCatalog& catalog, std::span<const BindingAsset> bindings,
                                        const ExplorerQuery& query) const
{
    const std::vector<Entry> entries = Entries(catalog, bindings);
    const bool filtering = !query.Filter.empty();
    ExplorerPage page;
    page.Actions = { "ui_preview.rescan" };
    page.Empty = "Nothing in the vocabulary holds that.";
    // Depth of the shallowest closed branch being skipped, or -1.
    int skipping = -1;
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const Entry& entry = entries[i];
        if (skipping >= 0 && entry.Depth > skipping)
            continue;
        skipping = -1;
        const bool branch = !entry.Branch.empty();
        if (branch)
        {
            bool any = !filtering;
            for (std::size_t j = i + 1; !any && j < entries.size() && entries[j].Depth > entry.Depth; ++j)
                any = entries[j].Branch.empty() && TextFilterMatch(entries[j].Label, query.Filter);
            const bool open = filtering || !Closed.contains(entry.Branch);
            if (!any || !open)
                skipping = entry.Depth;
            if (any && CountOnPage(page, query))
                page.Items.push_back({ .Key = entry.Branch,
                                       .Label = entry.Label,
                                       .Depth = entry.Depth,
                                       .Branch = open ? ExplorerBranch::Open : ExplorerBranch::Closed });
            continue;
        }
        if (TextFilterMatch(entry.Label, query.Filter) && CountOnPage(page, query))
            page.Items.push_back({ .Key = std::format("entry:{}", i), .Label = entry.Label, .Depth = entry.Depth });
    }
    return page;
}

bool VocabularyOutline::Flip(std::string_view key)
{
    if (!key.starts_with(kBranchKey))
        return false;
    if (const auto closed = Closed.find(key); closed != Closed.end())
        Closed.erase(closed);
    else
        Closed.emplace(key);
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
