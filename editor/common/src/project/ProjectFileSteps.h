#pragma once

#include "documents/DocumentSource.h"
#include "project/ProjectFiles.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class DocumentSourceSet;

// `text` -- a content document, JSON or JSON5 -- naming the asset that moved
// from `from` to `to` where it names it: a bare path becomes {id, path} at the
// new path, the path of an {id, path} reference becomes the new path, and a
// part of the file (`#anim:Walk`) or a moved scene's cooked form (.smap) moves
// with it. Comments and everything else stay exactly as written; nullopt when
// `text` names the asset nowhere.
[[nodiscard]] std::optional<std::string> NamingMovedAsset(std::string_view text, std::string_view from,
                                                          std::string_view to, AssetId id);

// The first free "<name> copy", "<name> copy 2"... beside the file at `from`,
// inserted before the first dot of its name, so "move.profile.sdata" copies to
// "move copy.profile.sdata".
[[nodiscard]] std::string CopyPath(std::string_view from, const std::function<bool(const std::string&)>& taken);

// What a file step did to the project's files, for whoever keeps a view of them
// (the registry, the cook's cache, open documents, the shelf).
struct ProjectFileChange
{
    enum class Kind : std::uint8_t
    {
        Gone,
        Appeared,
    };

    Kind What = Kind::Gone;
    std::string AssetPath;
    std::filesystem::path File;
    // The content root holding it.
    std::filesystem::path Root;
};

// What file steps ask of whoever owns the project's files.
struct ProjectFileHooks
{
    // Whether an editor holds the file open. A file it holds follows a move
    // (DocumentSource::FileMoved), but is not trashed; nor is a file moved
    // that an open one names, which holds its old path.
    std::function<bool(const ProjectFile&)> IsOpen = {};
    // Whether the file is an asset (IsAssetFile), and so goes by an id.
    std::function<bool(const ProjectFile&)> IsAsset = {};
    std::function<void(const ProjectFileChange&)> Changed = {};
};

//=============================================================================
// ProjectFileSteps
//
// Moving, renaming, duplicating and trashing a project's content files, each
// one step in the journal every document shares, so Ctrl+Z puts a file back as
// it puts back an edit. A file moves with its sidecar and keeps the id it
// states (it is given one first, if it states none); every document that still
// names it by its old path is made to name it by its id and new path, and an
// open document living in it follows it. Trashed files go to `.trash` under
// their root, which no walk over content enters.
//=============================================================================
class ProjectFileSteps final : public DocumentSource
{
public:
    ProjectFileSteps(std::vector<std::filesystem::path> roots, DocumentSourceSet& journal, ProjectFileHooks hooks);
    ~ProjectFileSteps() override;

    ProjectFileSteps(const ProjectFileSteps&) = delete;
    ProjectFileSteps& operator=(const ProjectFileSteps&) = delete;

    [[nodiscard]] std::span<const std::filesystem::path> ContentRoots() const { return Roots; }
    // Counts every step taken, undone or redone, for views of the files to
    // know when they are out of date.
    [[nodiscard]] std::uint64_t Version() const { return Taken; }

    // Each returns why it did nothing; empty when it was done.
    [[nodiscard]] std::string Move(std::string_view assetPath, std::string_view toAssetPath);
    [[nodiscard]] std::string Duplicate(std::string_view assetPath, std::string_view toAssetPath);
    [[nodiscard]] std::string Trash(std::string_view assetPath);

    // DocumentSource: a file step has no unsaved state of its own.
    void AppendChangedDocuments(std::vector<DocumentRef>&) override {}
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view) override { return {}; }
    [[nodiscard]] bool SettleDocument(std::string_view, ConflictChoice, std::string&) override { return true; }
    void StepDocument(std::string_view key, DocumentStep step) override;
    void CancelDocumentEdits() override {}
    void DiscardDocument(std::string_view) override {}
    [[nodiscard]] std::string DocumentLabel(std::string_view key) const override;

private:
    // One file rewritten by a step: what it held before and after.
    struct Rewrite
    {
        std::filesystem::path File;
        std::optional<std::string> Before;
        std::optional<std::string> After;
    };
    // One file renamed by a step.
    struct Rename
    {
        std::filesystem::path From;
        std::filesystem::path To;
    };
    struct Step
    {
        std::string Label;
        // Taken in order to do the step, and in reverse with sides swapped to undo it.
        std::vector<Rewrite> Before;
        std::vector<Rename> Renames;
        std::vector<Rewrite> After;
        std::vector<ProjectFileChange> Changes;
        // A move: what open documents follow, after the changes are known.
        std::optional<MovedFile> Moved;
    };

    enum class OpenFiles : std::uint8_t
    {
        Follow,
        Refuse,
    };
    [[nodiscard]] std::string Refusal(std::string_view assetPath, OpenFiles open,
                                      std::optional<ProjectFile>& file) const;
    [[nodiscard]] std::filesystem::path RootOf(const std::filesystem::path& file) const;
    [[nodiscard]] std::filesystem::path FileAt(const ProjectFile& source, std::string_view assetPath) const;
    void Take(const Step& step, DocumentStep direction);
    void Record(Step step);

    std::vector<std::filesystem::path> Roots;
    DocumentSourceSet& Journal;
    ProjectFileHooks Hooks;
    std::vector<Step> Steps;
    std::uint64_t Taken = 0;
};
