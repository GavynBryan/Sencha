#include "AbilityPhaseStrip.h"

#include "ui/DocumentLengths.h"

#include <ui/UiService.h>

namespace
{
const UiModelPropertyId kLeft = UiPropertyIdAt(0);
const UiModelPropertyId kTop = UiPropertyIdAt(1);
const UiModelPropertyId kWidth = UiPropertyIdAt(2);
const UiModelPropertyId kHeight = UiPropertyIdAt(3);
const UiModelPropertyId kEmpty = UiPropertyIdAt(4);
const UiModelRowsId kCards = UiRowsIdAt(0);
const UiModelRowsId kSlots = UiRowsIdAt(1);
const UiModelRowsId kOps = UiRowsIdAt(2);
const UiModelRowsId kLinks = UiRowsIdAt(3);

const UiActionId kPick = UiActionIdAt(0);
const UiActionId kCarry = UiActionIdAt(1);
const UiActionId kDrop = UiActionIdAt(2);
const UiActionId kPut = UiActionIdAt(3);
const UiActionId kAdd = UiActionIdAt(4);
const UiActionId kAddPhase = UiActionIdAt(5);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://ability_strip.rml";
    desc.ModelName = "ability_strip";
    const auto pixels = [](const char* name) { return UiModelProperty{ name, UiValue(std::string("0px")) }; };
    desc.Properties = { pixels("left"), pixels("top"), pixels("width"), pixels("height"),
                        UiModelProperty{ "empty", UiValue(std::string{}) } };
    desc.RowLists = { "cards", "slots", "ops", "links" };
    desc.Actions = { "strip_pick", "strip_carry", "strip_drop", "strip_put", "strip_add", "strip_add_phase" };
    return desc;
}

bool SameRows(const AbilityStripRows& a, const AbilityStripRows& b)
{
    return a.Cards == b.Cards && a.Slots == b.Slots && a.Ops == b.Ops && a.Links == b.Links;
}
} // namespace

AbilityPhaseStrip::AbilityPhaseStrip(UiService& ui)
    : Ui(ui)
    , Screen(ui, Describe)
{
}

AbilityPhaseStrip::~AbilityPhaseStrip()
{
    Screen.Close();
}

void AbilityPhaseStrip::MoveTo(UiSurfaceId surface)
{
    if (surface == Screen.Surface())
        return;
    Screen.Close();
    Screen.MoveTo(surface);
    Published = false;
    if (Visible)
        (void)Screen.Open();
}

void AbilityPhaseStrip::SetVisible(bool visible)
{
    Visible = visible;
    Published = false;
    if (visible)
        (void)Screen.Open();
    else
        Screen.Close();
}

std::vector<AbilityStripRequest> AbilityPhaseStrip::Update(const Rect2d& region, const AbilityStripRows& rows,
                                                           std::string_view empty)
{
    std::vector<AbilityStripRequest> requests;
    if (!Screen.IsOpen())
        return requests;
    for (const UiAction& action : Ui.DrainActions(Screen.Handle()))
    {
        const auto key = [&] { return std::string(action.Arg(0).AsString()); };
        if (action.Id == kPick)
            requests.push_back({ AbilityStripAsk::Pick, key() });
        else if (action.Id == kCarry)
            requests.push_back({ AbilityStripAsk::Carry, key() });
        else if (action.Id == kDrop)
            requests.push_back({ AbilityStripAsk::Drop, key() });
        else if (action.Id == kPut)
            requests.push_back({ AbilityStripAsk::Put });
        else if (action.Id == kAdd)
            requests.push_back({ AbilityStripAsk::Add, key() });
        else if (action.Id == kAddPhase)
            requests.push_back({ AbilityStripAsk::AddPhase });
    }

    const UiScreenHandle screen = Screen.Handle();
    (void)Ui.SetValue(screen, kLeft, UiValue(PixelLength(region.Position.X)));
    (void)Ui.SetValue(screen, kTop, UiValue(PixelLength(region.Position.Y)));
    (void)Ui.SetValue(screen, kWidth, UiValue(PixelLength(region.Width())));
    (void)Ui.SetValue(screen, kHeight, UiValue(PixelLength(region.Height())));
    (void)Ui.SetValue(screen, kEmpty, UiValue(std::string(empty)));
    if (Published && SameRows(rows, Shown))
        return requests;
    (void)Ui.SetRows(screen, kCards, rows.Cards);
    (void)Ui.SetRows(screen, kSlots, rows.Slots);
    (void)Ui.SetRows(screen, kOps, rows.Ops);
    (void)Ui.SetRows(screen, kLinks, rows.Links);
    Shown = rows;
    Published = true;
    return requests;
}
