#pragma once

#include "document/EntityChangeSet.h"
#include "operations/EditorOperations.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class CommandStack;
class EditorDocument;
class WorldDocument;

enum class LevelDraftState : std::uint8_t
{
    Open,
    // A step was refused; the draft holds what the steps before it made.
    Failed,
};

struct LevelDraftStep
{
    std::string Operation;
    JsonValue Arguments;
};

// A proposal being written: a copy of a level that mutations edit instead of
// the level, the steps that edited it, and what they changed.
struct LevelDraft
{
    std::string Id;
    std::string Label;
    std::string Rationale;
    // The copied level: its asset path for files (empty when never saved), and
    // the document itself while this session lasts.
    std::string BasePath;
    std::uint64_t BaseInstance = 0;
    std::uint32_t IdSeed = 0;
    // The proposal file it was opened from; empty when it was begun here.
    std::string OpenedFrom;
    std::unique_ptr<EditorDocument> Document;
    EntityChangeSet Changes;
    std::vector<LevelDraftStep> Steps;
    LevelDraftState State = LevelDraftState::Open;
    std::string Failure;
};

// The level the level operations read and write: the draft being written, or
// else the world document's focus level, where each mutation is one undo step
// on `commands`.
class LevelTarget
{
public:
    // Edits the document, telling the recorder what it touches, and answers.
    using Edit = std::function<EditorOperationOutcome(EditorDocument&, EntityChangeRecorder&)>;
    using Replay = std::function<EditorOperationOutcome(const LevelDraftStep&)>;

    LevelTarget(WorldDocument& world, CommandStack& commands);
    ~LevelTarget();

    [[nodiscard]] EditorDocument& Document();
    [[nodiscard]] const EditorDocument& Document() const;
    // The level itself, whatever draft is being written.
    [[nodiscard]] const EditorDocument& FocusLevel() const;
    [[nodiscard]] WorldDocument& LevelWorld() { return World; }

    // Runs `edit` as step `operation` with `arguments`: a refusal puts back what
    // it had already changed (and fails a draft), and success adds what changed
    // and the revisions to its answer.
    [[nodiscard]] EditorOperationOutcome Mutate(std::string_view operation, const JsonValue& arguments,
                                                const Edit& edit);
    // The same for a tool's edit: always the level itself, whatever draft an
    // operation client is writing, and never a step of one.
    [[nodiscard]] EditorOperationOutcome MutateLevel(const Edit& edit);

    // Copies the focus level into a new draft and writes to it. Null, with
    // `error` set, when the copy did not load.
    LevelDraft* BeginDraft(std::string label, std::string rationale, std::uint32_t idSeed, std::string& error);
    [[nodiscard]] LevelDraft* FindDraft(std::string_view id);
    [[nodiscard]] LevelDraft* WritingDraft() { return Writing; }
    [[nodiscard]] std::span<const std::unique_ptr<LevelDraft>> Drafts() const { return Drafts_; }
    // Null writes to the level itself.
    void WriteTo(LevelDraft* draft) { Writing = draft; }
    // The draft Kyusu draws over its level: the newest begun until another is
    // chosen; null shows none.
    [[nodiscard]] const LevelDraft* PreviewedDraft() const { return Previewed; }
    void Preview(LevelDraft* draft) { Previewed = draft; }
    // The open level `draft` copies; null once that level has closed.
    [[nodiscard]] const EditorDocument* BaseOf(const LevelDraft& draft) { return FindBase(draft); }

    // Applies the draft's changes to its level as one undo step and drops it,
    // unless the level has moved on under any entity the draft changes.
    [[nodiscard]] EditorOperationOutcome AcceptDraft(LevelDraft& draft);
    void DiscardDraft(LevelDraft& draft);
    // Drops every draft whose level is no longer open, and says which by label.
    std::vector<std::string> DropOrphanedDrafts();
    // Replays the draft's steps through `replay` on a fresh copy of its level.
    [[nodiscard]] EditorOperationOutcome RebaseDraft(LevelDraft& draft, const Replay& replay);

private:
    [[nodiscard]] EditorOperationOutcome Run(LevelDraft* draft, std::string_view operation,
                                             const JsonValue& arguments, const Edit& edit);
    std::unique_ptr<LevelDraft> CopyLevel(EditorDocument& level, std::string label, std::string rationale,
                                          std::uint32_t idSeed, std::string& error);
    [[nodiscard]] EditorDocument* FindBase(const LevelDraft& draft);

    WorldDocument& World;
    CommandStack& Commands;
    std::vector<std::unique_ptr<LevelDraft>> Drafts_;
    LevelDraft* Writing = nullptr;
    LevelDraft* Previewed = nullptr;
    std::uint32_t DraftsBegun = 0;
};

// The entities a change set created, modified and deleted, by id and name, the
// wall brushes spaces built counted apart, and its schema followed by `extra`.
[[nodiscard]] JsonValue::Object ChangeSummary(const EntityChangeSet& changes);
// Whether the change is to a wall brush a space built, as it now stands.
[[nodiscard]] bool ChangesGeneratedWall(const EntityChange& change);
[[nodiscard]] DataFieldSchema ChangeSummarySchema(std::vector<DataFieldSchema> extra = {});

// What every mutation's answer adds: created, modified and deleted entities and
// the revision before and after, followed by `extra`.
[[nodiscard]] DataFieldSchema MutationResultSchema(std::vector<DataFieldSchema> extra = {});
