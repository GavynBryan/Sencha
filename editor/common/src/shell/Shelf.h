#pragma once

#include "actions/ActionContext.h"
#include "project/AssetQuery.h"
#include "project/ProjectFiles.h"
#include "prompts/PromptQueue.h"
#include "workspaces/WorkspaceKind.h"

#include <graphics/PresentationId.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class AssetPictures;
class NoticeQueue;
class ProjectFileSteps;
class ReferenceIndex;
class UiService;
class WorkspaceHost;
struct ShellWindowServices;
struct UiAction;

// A shelf raised to choose one file for something rather than to open one:
// only the files it offers, the one in use marked, and the choice handed back.
// A click picks; Enter, a double click or CHOOSE commits, and it goes.
struct ShelfPick
{
    std::string Title;
    std::vector<ContentKind> Accepts = {};
    // Exactly these files, when given, whatever their kind.
    std::vector<std::string> Only = {};
    std::string Current = {};
    std::vector<std::string> Withheld = {};
    // Choosing nothing is a choice: a field cleared.
    bool OffersNone = false;
    // `assetPath`, empty for none, picked or committed to.
    std::function<void(const std::string& assetPath, bool commit)> Choose = {};

    [[nodiscard]] bool Offers(const ProjectFile& file) const;
};

// Whether the shelf's search field edits with `chord`, and so keeps it rather
// than letting the window's actions have it: every plain key but the function
// keys (and Delete, when there is nothing to delete), and with Ctrl only
// selecting, the clipboard and moving or deleting by word.
[[nodiscard]] bool SearchFieldKeeps(const KeyChord& chord, bool empty);
// Where `written` -- a place in the project without its scheme or extension,
// as a rename asks for it -- puts the file at `from`; empty for nowhere.
[[nodiscard]] std::string RenamedPath(std::string_view from, std::string_view written);

// Where a shelf finds the project's content, and what it is told about it.
struct ShelfSources
{
    // Where the open project's content is on disk; none without a project.
    std::vector<std::filesystem::path> ContentRoots = {};
    // Content mounted beside the project (the engine's), which the project's
    // files may name but the shelf does not show.
    std::vector<std::filesystem::path> OtherRoots = {};
    // Without pictures, every file is shown by its kind's icon.
    AssetPictures* Pictures = nullptr;
    // Without references, nothing is said of what uses what.
    ReferenceIndex* References = nullptr;
    // Without file steps, nothing is moved, copied or trashed from it.
    ProjectFileSteps* Files = nullptr;
};

//=============================================================================
// Shelf
//
// The project's content along the bottom of a window: every content file,
// found by typing, narrowed by kind, folder or list (recent, open, changed),
// shown as tiles or a list beside what the chosen one is, and opened in the
// workspace that opens its kind. It belongs to the window, not to a tab, so
// what it was showing is still there when it comes back.
//
// Raised, it lies over the work and goes once something is opened or Escape
// is pressed; pinned, it stays and the work ends above it. Its top edge sizes
// it. The chosen file is renamed, duplicated and trashed through the edit
// actions, which it claims while raised and answers from its search field
// whenever it has the keys.
//=============================================================================
class Shelf
{
public:
    Shelf(const ShellWindowServices& services, UiSurfaceId surface);
    ~Shelf();

    Shelf(const Shelf&) = delete;
    Shelf& operator=(const Shelf&) = delete;

    // Raised, reading the project's content afresh; narrowed to `category`
    // when one is given.
    void Show(std::string category = {});
    void Hide();
    void Toggle();
    // Raised to choose for `pick`, in place of opening.
    void Pick(ShelfPick pick);
    // Raised with `assetPath` chosen and nothing narrowing it out of sight.
    void Reveal(const std::string& assetPath);
    // What it is choosing for now; empty while it opens what is chosen.
    [[nodiscard]] const std::string& Choosing() const { return Picking.Title; }
    [[nodiscard]] bool IsRaised() const { return State == Placement::Raised; }
    [[nodiscard]] bool IsPinned() const { return State == Placement::Pinned; }

    // Per frame: act on what was done to it, then publish what it shows.
    // `dp` is the surface's scale; `windowHeight` bounds how tall it may be.
    void Update(float dp, float windowHeight);
    // How far up from the window's bottom edge it reaches, in window pixels;
    // nothing while it is down.
    [[nodiscard]] float Reach() const;

private:
    enum class Placement : std::uint8_t { Down, Raised, Pinned };
    enum class Listing : std::uint8_t { Everything, Recent, Open, Changed, Unused, Problems };
    enum class Look : std::uint8_t { Tiles, List };
    enum class Edge : std::uint8_t { Resting, Sizing };
    enum class Purpose : std::uint8_t { Opening, Choosing };

    void HandleAction(const UiAction& action, float windowHeight);
    // A chord the search field had no use for, answered as the window would.
    void PassOn(const UiAction& action);
    void OfferFileActions();
    [[nodiscard]] bool CanChangeFiles() const;
    void AskRename();
    void Rename(const std::string& from, std::string_view written);
    void DuplicateChosen();
    void AskTrash();
    void Trash(const std::string& assetPath);
    void Tell(std::string text, bool failed);
    void Rescan();
    void Remember();
    // Opens the file, or commits to it while choosing.
    void Open(const std::string& assetPath);
    [[nodiscard]] bool Offers(const ProjectFile& file) const;
    [[nodiscard]] bool Listed(const ProjectFile& file) const;
    // Shows a file the chosen one names or is named by, leaving the way back.
    void Follow(const std::string& assetPath);
    void Move(int by);
    void Publish();
    void PublishReferences(const ProjectFile* chosen);
    [[nodiscard]] std::vector<const ProjectFile*> Shown() const;
    [[nodiscard]] const ProjectFile* FileAt(const std::string& assetPath) const;
    [[nodiscard]] int Columns() const;

    UiService& Ui;
    UiSurfaceId Surface;
    WorkspaceHost& Workspaces;
    PresentationId Presentation;
    ShelfSources Sources;
    ActionCatalog& Catalog;
    ActionContextStack& Actions;
    NoticeQueue& Notices;
    UiScreenHandle Screen;
    ActionContext FileActions;
    ActionContextStack::Layer FileLayer;
    PromptQueue::Asker Asker;

    Placement State = Placement::Down;
    Purpose Use = Purpose::Opening;
    ShelfPick Picking;
    Listing List = Listing::Everything;
    Look View = Look::Tiles;
    AssetQuery Query;
    std::vector<ProjectFile> Files;
    // The file steps' version the files were read at.
    std::uint64_t FilesVersion = 0;
    std::string Selected;
    std::set<std::string> OpenFolders;
    // Most recent first: what this window has shown, opened from here or not.
    std::vector<std::string> Recent;
    // The files followed from, the latest last; the back key returns to it.
    std::vector<std::string> Trail;

    float Dp = 1.0f;
    float HeightDp = 300.0f;
    // The side of a tile's picture; Ctrl and the wheel over the tiles size it.
    float PictureDp = 72.0f;
    // The top edge being dragged: where the pointer and the edge began.
    Edge TopEdge = Edge::Resting;
    float GrabbedAt = 0.0f;
    float GrabbedHeightDp = 0.0f;
};
