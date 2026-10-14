#pragma once

#include "properties/PropertyNode.h"
#include "shell/SheetPoint.h"
#include "shell/TransientSurfaces.h"

#include <graphics/PresentationId.h>
#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class ActionCatalog;
class ActionContextStack;
class ProblemList;
class ScreenModel;
class SdlWindow;
class UiService;
class WorkspaceHost;
struct UiAction;

struct RailMenusHost
{
    UiService& Ui;
    ActionCatalog& Catalog;
    ActionContextStack& Actions;
    WorkspaceHost& Workspaces;
    PresentationId Presentation;
    ProblemList& Problems;
    TransientSurfaces& Transients;
    SdlWindow& Window;
    std::function<void()> TakeEscape;
    // A listed problem chosen: show where it is.
    std::function<void(std::string_view index)> RevealProblem;
    // One of a sheet row's choices chosen for its verb.
    std::function<void(SheetPoint at, std::string_view verb, std::string_view choice)> ChooseForSheet;
};

// The rail's popovers: everything the window can do by category (the hatch),
// the tools, one category's actions, the problems, a sheet row's choices, and
// whether the colour wheel is the one up. One is open at a time; a press
// elsewhere or Escape closes it.
class RailMenus
{
public:
    enum class Popover : std::uint8_t { None, Hatch, Tools, Category, Choices, Problems, Color };

    explicit RailMenus(RailMenusHost host);

    void Declare(ScreenModel& model);
    void Open(UiScreenHandle screen) { Screen = screen; }

    // Whether `action` was one of the popovers'.
    bool Handle(const UiAction& action);
    void Publish();

    void Show(Popover popover, std::string category = {});
    // As a document or a script names one: "hatch", "tools", "problems",
    // "category:Grid"; anything else closes what is open.
    void ShowNamed(std::string_view name);
    void ShowChoices(SheetPoint at, PropertyVerb verb);
    // Whether the transient that closed was this one; it is shut if so.
    bool Closed(TransientSurfaces::Id id);

    [[nodiscard]] Popover Showing() const { return Shown; }
    [[nodiscard]] TransientSurfaces::Id Transient() const { return OpenTransient; }

private:
    [[nodiscard]] std::string Name() const;
    void Choose(const std::string& key);
    [[nodiscard]] std::vector<UiRow> HatchEntries() const;
    [[nodiscard]] std::vector<UiRow> CategoryEntries(std::string_view category) const;
    [[nodiscard]] std::vector<UiRow> ToolEntries() const;
    [[nodiscard]] std::vector<UiRow> ProblemEntries() const;
    [[nodiscard]] std::vector<UiRow> ChoiceEntries() const;
    // Under its instrument, or rising from one in the lower half, inside the window.
    void Place(std::string_view anchor);

    RailMenusHost Host;
    UiScreenHandle Screen;
    Popover Shown = Popover::None;
    std::string Category;
    TransientSurfaces::Id OpenTransient = 0;
    SheetPoint ChoicesAt;
    PropertyVerb Choosing;
    // What has been typed to narrow the choices.
    std::string ChoicesFilter;

    UiModelPropertyId PopoverName;
    UiModelPropertyId X;
    UiModelPropertyId Rises;
    UiModelPropertyId Top;
    UiModelPropertyId Filters;
    UiModelPropertyId Tallest;
    UiModelRowsId Entries;
    UiActionId PopoverAction;
    UiActionId ChooseAction;
    UiActionId FilterAction;
};
