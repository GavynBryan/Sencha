#pragma once

#include "input/InputEvent.h"
#include "presenters/PropertySheet.h"
#include "shell/NumberScrub.h"
#include "shell/SheetPoint.h"

#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class ActionCatalog;
class ActionContextStack;
class NoticeQueue;
class ScreenModel;
class Shelf;
class UiService;
struct UiAction;

// What the sheets need from the window they are in.
struct SheetPanelsHost
{
    UiService& Ui;
    ActionCatalog& Catalog;
    ActionContextStack& Actions;
    NoticeQueue& Notices;
    Shelf& Assets;
    // The properties behind a sheet's rows, asked for again each time: the
    // workspace's frame may have moved on since they were presented.
    std::function<std::vector<PropertySection>(SheetId)> Sections;
    // A verb's choices, picked from before it acts.
    std::function<void(SheetPoint, std::string verb, std::vector<std::string> choices,
                       std::vector<std::string> notes)>
        OpenChoices;
    std::function<void(SheetPoint)> OpenColor;
    // Hands the rest of a press to the shell; false when it was already let go.
    std::function<bool()> TakePress;
    // A drag taking the pointer for itself, and letting go of it.
    std::function<void(bool)> CapturePointer;
};

// The tool card's sheet and the selection's: their rows, and what a person does
// to a row -- set it, flip it, act on it, fold it, type into or drag a number,
// pick an asset for it or drop one on it.
class SheetPanels
{
public:
    explicit SheetPanels(SheetPanelsHost host);

    void Declare(ScreenModel& model);
    void Open(UiScreenHandle screen) { Screen = screen; }

    // Whether `action` was one of the sheets'.
    bool Handle(const UiAction& action);
    [[nodiscard]] bool HoldsInput() const { return Scrub.Holds(); }
    bool TakeHeldInput(const InputEvent& event);

    // A sheet's rows presented, and how many; nothing while a field of it is held.
    std::optional<std::size_t> Present(SheetId sheet, std::span<const PropertySection> sections, bool everything);
    // What a sheet showed has gone; anything held, typed or dragged in it
    // applied to that, and ends unapplied.
    void MovedOn(SheetId sheet);
    [[nodiscard]] bool Holding(SheetId sheet) const { return Sheet(sheet).Holding(); }

    // A verb done on a row: what choosing from its choices does.
    void Act(SheetPoint at, std::string_view verb, std::string_view choice);
    [[nodiscard]] std::optional<UiRow> Shown(SheetPoint at) const;
    [[nodiscard]] std::string EditKey(SheetPoint at) const { return Sheet(at.Sheet).EditKeyOf(at.Row); }
    [[nodiscard]] std::string Label(SheetPoint at) const { return Sheet(at.Sheet).LabelOf(at.Row); }
    [[nodiscard]] IPropertySource* SourceAt(SheetPoint at) const;

private:
    [[nodiscard]] PropertySheet& Sheet(SheetId sheet) { return Sheets[static_cast<std::size_t>(sheet)]; }
    [[nodiscard]] const PropertySheet& Sheet(SheetId sheet) const
    {
        return Sheets[static_cast<std::size_t>(sheet)];
    }
    [[nodiscard]] UiModelRowsId RowsOf(SheetId sheet) const;
    [[nodiscard]] std::vector<UiRow> ShownRows(SheetId sheet) const;
    void PressNumber(SheetPoint at);
    // What was typed into a row's number set as one step; how many numbers it has.
    std::size_t FinishTyping(SheetPoint at);
    void PickAsset(SheetPoint at, bool follows);
    void DropCarried(SheetPoint at);
    void PublishTyping();

    SheetPanelsHost Host;
    UiScreenHandle Screen;
    std::array<PropertySheet, 2> Sheets;
    NumberScrub Scrub;

    UiModelPropertyId TypingId;
    UiModelRowsId ToolRows;
    UiModelRowsId SelectionRows;
    UiActionId Set;
    UiActionId Flip;
    UiActionId Hold;
    UiActionId Release;
    UiActionId ActOn;
    UiActionId Pick;
    UiActionId Fold;
    UiActionId Press;
    UiActionId ReleasePart;
    UiActionId Key;
    UiActionId Drop;
    UiActionId Color;
};
