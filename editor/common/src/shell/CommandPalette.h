#pragma once

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class ActionCatalog;
class ActionContextStack;
class UiService;
class WorkspaceHost;

// Something the palette can do: an action in play, or a workspace to go to.
struct CommandCandidate
{
    std::string Key;
    std::string Label;
    std::string Category;
    std::string Chord = {};
};

// The candidates that match `query`, best first, at most `limit`. A label that
// starts with the query beats one with a word that does, which beats one that
// merely contains it; a category match comes after every label match. Ties
// keep the candidates' own order, so the list reads the same from one
// keystroke to the next.
[[nodiscard]] std::vector<std::size_t> RankCommands(std::span<const CommandCandidate> candidates,
                                                    std::string_view query, std::size_t limit);

//=============================================================================
// CommandPalette
//
// Everything the window can do, found by typing: the actions claimed in the
// window's stack and the workspaces to go to. Enter performs the highlighted
// one; Up and Down move the highlight; Escape or a press elsewhere leaves.
//=============================================================================
class CommandPalette
{
public:
    CommandPalette(UiService& ui, UiSurfaceId surface, const ActionCatalog& catalog,
                   ActionContextStack& actions, WorkspaceHost& workspaces);
    ~CommandPalette();

    CommandPalette(const CommandPalette&) = delete;
    CommandPalette& operator=(const CommandPalette&) = delete;

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const { return Screen.IsValid(); }

    // Per frame while open: act on what the document asked for, then publish.
    void Update();

private:
    void Gather();
    void Publish();
    void Perform(std::size_t shownIndex);

    UiService& Ui;
    UiSurfaceId Surface;
    const ActionCatalog& Catalog;
    ActionContextStack& Actions;
    WorkspaceHost& Workspaces;

    UiScreenHandle Screen;
    std::vector<CommandCandidate> Candidates;
    std::vector<std::size_t> Shown;
    std::string Query;
    std::size_t Highlight = 0;
};
