#pragma once

#include "project/AssetQuery.h"
#include "project/ProjectFiles.h"
#include "workspaces/WorkspaceKind.h"

#include <graphics/PresentationId.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

class AssetPictures;
class UiService;
class WorkspaceHost;
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
// it.
//=============================================================================
class Shelf
{
public:
    // Without `pictures`, every file is shown by its kind's icon.
    Shelf(UiService& ui, UiSurfaceId surface, WorkspaceHost& workspaces, PresentationId presentation,
          std::vector<std::filesystem::path> contentRoots, AssetPictures* pictures);
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
    enum class Listing : std::uint8_t { Everything, Recent, Open, Changed };
    enum class Look : std::uint8_t { Tiles, List };
    enum class Edge : std::uint8_t { Resting, Sizing };
    enum class Purpose : std::uint8_t { Opening, Choosing };

    void HandleAction(const UiAction& action, float windowHeight);
    void Rescan();
    void Remember();
    // Opens the file, or commits to it while choosing.
    void Open(const std::string& assetPath);
    [[nodiscard]] bool Offers(const ProjectFile& file) const;
    void Move(int by);
    void Publish();
    [[nodiscard]] std::vector<const ProjectFile*> Shown() const;
    [[nodiscard]] const ProjectFile* FileAt(const std::string& assetPath) const;
    [[nodiscard]] int Columns() const;

    UiService& Ui;
    UiSurfaceId Surface;
    WorkspaceHost& Workspaces;
    PresentationId Presentation;
    std::vector<std::filesystem::path> Roots;
    AssetPictures* Pictures;
    UiScreenHandle Screen;

    Placement State = Placement::Down;
    Purpose Use = Purpose::Opening;
    ShelfPick Picking;
    Listing List = Listing::Everything;
    Look View = Look::Tiles;
    AssetQuery Query;
    std::vector<ProjectFile> Files;
    std::string Selected;
    std::set<std::string> OpenFolders;
    // Most recent first: what this window has shown, opened from here or not.
    std::vector<std::string> Recent;

    float Dp = 1.0f;
    float HeightDp = 300.0f;
    // The side of a tile's picture; Ctrl and the wheel over the tiles size it.
    float PictureDp = 72.0f;
    // The top edge being dragged: where the pointer and the edge began.
    Edge TopEdge = Edge::Resting;
    float GrabbedAt = 0.0f;
    float GrabbedHeightDp = 0.0f;
};
