#pragma once

#include "workspaces/ExplorerTable.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

class ActionCatalog;
class ActionContext;
class EditorDocument;
class EditorOperations;
class LevelTarget;
class NoticeQueue;
struct LevelDraft;

// The proposals before the designer: the open drafts, each with what it would
// change and how it moves the level's diagnostics, and the proposal files in
// the project's .proposals folder not yet opened. Picking a draft shows it over
// the level; picking a file opens it. Accept and reject act on the draft shown,
// through the same draft operations an agent or the console uses.
class ProposalTray
{
public:
    static constexpr std::string_view kId = "proposals";

    // `proposals` is <project>/.proposals; empty without a project.
    ProposalTray(LevelTarget& level, EditorOperations& operations, NoticeQueue& notices,
                 std::filesystem::path proposals);

    [[nodiscard]] ExplorerRow Row();
    // proposal.accept and proposal.reject.
    void Offer(ActionCatalog& catalog, ActionContext& context);

private:
    ExplorerPage Explore(const ExplorerQuery& query);
    void Choose(std::string_view key, ExplorerChoice choice);
    // Runs a draft operation on the shown draft and says how it went.
    void Settle(std::string_view operation, std::string_view done);
    // How many more problems the draft has than its base, counted again only
    // when either has changed: the tray is published every frame.
    long ProblemDelta(const LevelDraft& draft, const EditorDocument& base);
    std::string Summary(const LevelDraft& draft);

    struct CountedDelta
    {
        std::uint64_t DraftRevision = 0;
        std::uint64_t BaseInstance = 0;
        std::uint64_t BaseRevision = 0;
        long          Delta = 0;
    };
    std::unordered_map<std::string, CountedDelta> Deltas;

    LevelTarget& Level;
    EditorOperations& Operations;
    NoticeQueue& Notices;
    std::filesystem::path Proposals;
};
