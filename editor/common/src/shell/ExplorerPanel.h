#pragma once

#include "shell/TransientSurfaces.h"
#include "workspaces/WorkspaceFacets.h"

#include <graphics/PresentationId.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

class ActionCatalog;
class ActionContext;
class ActionContextStack;
class IWorkspace;
class ScreenModel;
class Shelf;
class UiService;
class WorkspaceHost;
struct UiAction;

struct ExplorerPanelHost
{
    UiService& Ui;
    WorkspaceHost& Workspaces;
    PresentationId Presentation;
    ActionCatalog& Catalog;
    ActionContextStack& Actions;
    // Where the active workspace's explorer actions are claimed.
    ActionContext& WindowActions;
    TransientSurfaces& Transients;
    Shelf& Assets;
    // Escape is taken while anything transient is open.
    std::function<void()> TakeEscape;
    // Drawn over what it overlaps.
    std::function<void()> BringToFront;
};

// The active workspace's explorer, opened by its action or the breadcrumb: a
// page of its items, narrowed by a filter, chosen from by a click; an explorer
// of the project's files is the shelf choosing instead.
class ExplorerPanel
{
public:
    explicit ExplorerPanel(ExplorerPanelHost host);

    void Declare(ScreenModel& model);
    void Open(UiScreenHandle screen) { Screen = screen; }

    // The explorer's share of one drain of the document's actions.
    void Handle(std::span<const UiAction> actions);
    // Per frame: the active workspace's explorers claimed, the open one shown.
    void Publish(IWorkspace* workspace);

    // Opens `id` in place of any other; empty closes it.
    void Show(std::string id);
    void Close();
    // `id` opened if it is not, and `key` revealed in it.
    void Reveal(const std::string& id, const std::string& key);
    // Whether the transient that closed was this one; it is shut if so.
    bool Closed(TransientSurfaces::Id id);

    [[nodiscard]] const std::string& Shown() const { return ShownId; }
    [[nodiscard]] TransientSurfaces::Id Transient() const { return OpenTransient; }
    // Which shape is showing, as the floating layout keys it; empty for none.
    [[nodiscard]] const std::string& LayoutKey() const { return Key; }

private:
    void Claim(const std::vector<WorkspaceExplorerInfo>& explorers);
    void Choose(IWorkspace& workspace, const std::string& item, ExplorerChoice choice, std::size_t mark);

    ExplorerPanelHost Host;
    UiScreenHandle Screen;
    std::string ShownId;
    std::string Filter;
    std::size_t First = 0;
    TransientSurfaces::Id OpenTransient = 0;
    std::vector<std::string> Claimed;
    // The explorer the shelf is choosing for, while it is.
    std::string ChoosingFor;
    std::string Key;

    UiModelPropertyId IdProperty;
    UiModelPropertyId Title;
    UiModelPropertyId Shape;
    UiModelPropertyId Range;
    UiModelPropertyId Empty;
    UiModelPropertyId Earlier;
    UiModelPropertyId Later;
    UiModelRowsId Items;
    UiModelRowsId Verbs;
    UiActionId Explore;
    UiActionId FilterAction;
    UiActionId Page;
    UiActionId PickAction;
    UiActionId Commit;
    UiActionId CloseAction;
    UiActionId OpenAction;
    UiActionId Mark;
};
