#include "shell/SheetPanels.h"

#include "actions/ActionCatalog.h"
#include "notices/NoticeQueue.h"
#include "shell/Shelf.h"
#include "text/PathLeaf.h"
#include "ui/ScreenModel.h"

#include <ui/UiAction.h>
#include <ui/UiKeys.h>
#include <ui/UiService.h>

#include <algorithm>
#include <format>
#include <utility>

SheetPanels::SheetPanels(SheetPanelsHost host)
    : Host(std::move(host))
    , Scrub([this](bool held) {
        if (Host.CapturePointer)
            Host.CapturePointer(held);
    })
{
    for (PropertySheet& sheet : Sheets)
        sheet.OfferActions(Host.Catalog, Host.Actions);
}

void SheetPanels::Declare(ScreenModel& model)
{
    TypingId = model.Property("sheet_typing", UiValue(std::string{}));
    ToolRows = model.Rows("tool_rows");
    SelectionRows = model.Rows("selection_rows");
    Set = model.Action("shell_sheet_set");
    Flip = model.Action("shell_sheet_flip");
    Hold = model.Action("shell_sheet_hold");
    Release = model.Action("shell_sheet_release");
    ActOn = model.Action("shell_sheet_act");
    Pick = model.Action("shell_sheet_pick");
    Fold = model.Action("shell_sheet_fold");
    Press = model.Action("shell_scrub_press");
    ReleasePart = model.Action("shell_sheet_release_part");
    Key = model.Action("shell_sheet_key");
    Drop = model.Action("shell_sheet_drop");
    Color = model.Action("shell_sheet_color");
}

UiModelRowsId SheetPanels::RowsOf(SheetId sheet) const
{
    return sheet == SheetId::Tool ? ToolRows : SelectionRows;
}

std::vector<UiRow> SheetPanels::ShownRows(SheetId sheet) const
{
    return Host.Ui.GetRows(Screen, RowsOf(sheet));
}

std::optional<UiRow> SheetPanels::Shown(SheetPoint at) const
{
    std::vector<UiRow> rows = ShownRows(at.Sheet);
    if (at.Row >= rows.size())
        return std::nullopt;
    return std::move(rows[at.Row]);
}

IPropertySource* SheetPanels::SourceAt(SheetPoint at) const
{
    const std::vector<PropertySection> sections = Host.Sections(at.Sheet);
    return Sheet(at.Sheet).SourceOf(sections, at.Row);
}

bool SheetPanels::Handle(const UiAction& action)
{
    const UiActionId id = action.Id;
    if (id != Set && id != Flip && id != Hold && id != Release && id != ActOn && id != Pick && id != Fold
        && id != Press && id != ReleasePart && id != Key && id != Drop && id != Color)
        return false;
    const std::optional<SheetId> named = SheetNamed(action.Arg(0).AsString());
    const std::int64_t row = action.Arg(1).AsInt(-1);
    if (!named.has_value() || row < 0)
        return true;
    const SheetPoint at{ .Sheet = *named, .Row = static_cast<std::size_t>(row), .Element = action.Arg(2).AsInt(-1) };
    PropertySheet& sheet = Sheet(at.Sheet);
    const std::vector<PropertySection> sections = Host.Sections(at.Sheet);

    if (id == Color)
        Host.OpenColor(at);
    else if (id == Hold)
        sheet.Hold(at.Row);
    else if (id == Release)
    {
        const std::optional<UiRow> shown = Shown(at);
        (void)sheet.Release(sections, at.Row, shown.has_value() ? std::string_view(shown->Value) : std::string_view{});
    }
    else if (id == Flip)
        (void)sheet.Flip(sections, at.Row);
    else if (id == ActOn)
    {
        // A verb that asks for a choice first opens its choices under it.
        const std::string verb(action.Arg(2).AsString());
        const std::string chosen(action.Arg(3).AsString());
        const PropertyVerb* asked = sheet.VerbOf(at.Row, verb);
        if (asked != nullptr && !asked->Choices.empty() && chosen.empty())
            Host.OpenChoices({ at.Sheet, at.Row }, *asked);
        else
            Act({ at.Sheet, at.Row }, verb, chosen);
    }
    else if (id == Fold)
        sheet.Fold(at.Row);
    else if (id == Press)
        PressNumber(at);
    else if (id == ReleasePart)
    {
        // Let go of after Enter, Tab or Escape already ended it: nothing more.
        const std::optional<PropertySheet::TypedNumber> typing = sheet.Typing();
        if (typing.has_value() && typing->Row == at.Row && typing->Element == at.Element)
            (void)FinishTyping(at);
    }
    else if (id == Key)
    {
        const std::optional<UiKey> key = UiKeyFromEvent(action.Arg(3).AsInt());
        if (key == UiKey::Escape)
            sheet.Drop();
        else if (key == UiKey::Return || key == UiKey::Tab)
        {
            const std::size_t parts = FinishTyping(at);
            // Tab goes on to the next number of the same vector.
            if (key == UiKey::Tab && at.Element >= 0 && static_cast<std::size_t>(at.Element) + 1 < parts)
                sheet.Type(at.Row, at.Element + 1);
        }
    }
    else if (id == Drop)
        DropCarried({ at.Sheet, at.Row });
    else if (id == Pick)
        PickAsset({ at.Sheet, at.Row }, action.Arg(2).AsBool());
    else if (action.Arguments.size() > 2)
        (void)sheet.SetFromControl(sections, at.Row, action.Arg(2));
    return true;
}

void SheetPanels::Act(SheetPoint at, std::string_view verb, std::string_view choice)
{
    PropertySheet& sheet = Sheet(at.Sheet);
    const std::vector<PropertySection> sections = Host.Sections(at.Sheet);
    const bool removes = sheet.Removes(at.Row, verb);
    const std::string label = sheet.LabelOf(at.Row);
    if (sheet.Act(sections, at.Row, verb, choice) != PropertyEditResult::Applied || !removes)
        return;
    const std::string undo = Host.Catalog.ChordText("edit.undo");
    Host.Notices.Post(undo.empty() ? std::format("Removed {}", label)
                                   : std::format("Removed {} \xC2\xB7 {} puts it back", label, undo),
                      NoticeTone::Info, NoticeClockNow());
}

void SheetPanels::PressNumber(SheetPoint at)
{
    const std::optional<UiRow> shown = Shown(at);
    if (!shown.has_value() || !shown->Editable)
        return;
    const std::optional<std::size_t> part =
        at.Element >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(at.Element)) : std::nullopt;
    const std::string text = part.has_value() ? (*part < shown->Parts.size() ? shown->Parts[*part] : std::string{})
                                              : shown->Value;
    // The rest of the press is the shell's to read. One already let go was a
    // click, which types into the number instead.
    if (!Host.TakePress || !Host.TakePress())
    {
        Sheet(at.Sheet).Type(at.Row, at.Element);
        return;
    }
    Scrub.Press(at, text, shown->Parts, Sheet(at.Sheet).BoundsOf(at.Row, part));
}

bool SheetPanels::TakeHeldInput(const InputEvent& event)
{
    const NumberScrub::Took took = Scrub.Take(event, [this](const SheetPoint& at) {
        const PropertySheet& sheet = Sheet(at.Sheet);
        const std::optional<std::size_t> part =
            at.Element >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(at.Element)) : std::nullopt;
        std::string key = sheet.EditKeyOf(at.Row, part);
        // A number its source names no key for is moved by setting its vector whole.
        if (key.empty())
            key = sheet.EditKeyOf(at.Row);
        const bool whole = part.has_value() && key == sheet.EditKeyOf(at.Row);
        return NumberScrub::Target{ .Source = SourceAt(at), .Key = std::move(key), .WholeVector = whole };
    });
    if (took == NumberScrub::Took::Click)
        Sheet(Scrub.At().Sheet).Type(Scrub.At().Row, Scrub.At().Element);
    return took != NumberScrub::Took::No;
}

std::size_t SheetPanels::FinishTyping(SheetPoint at)
{
    PropertySheet& sheet = Sheet(at.Sheet);
    const std::optional<UiRow> shown = Shown(at);
    if (!shown.has_value())
    {
        sheet.Drop();
        return 0;
    }
    const std::vector<PropertySection> sections = Host.Sections(at.Sheet);
    if (at.Element < 0)
        (void)sheet.Release(sections, at.Row, shown->Value);
    else if (static_cast<std::size_t>(at.Element) < shown->Parts.size())
        (void)sheet.ReleaseElement(sections, at.Row, static_cast<std::size_t>(at.Element),
                                   shown->Parts[static_cast<std::size_t>(at.Element)]);
    else
        sheet.Drop();
    return shown->Parts.size();
}

void SheetPanels::DropCarried(SheetPoint at)
{
    // A file carried from the shelf, put on a field that can hold it.
    const std::string carried = Host.Assets.TakeCarried();
    if (carried.empty())
        return;
    PropertySheet& sheet = Sheet(at.Sheet);
    const std::vector<PropertySection> sections = Host.Sections(at.Sheet);
    const std::vector<std::string> fits = sheet.CandidatesOf(sections, at.Row);
    if (std::ranges::find(fits, carried) == fits.end())
        Host.Notices.Post(std::format("{} cannot go in {}", PathLeaf(carried), sheet.LabelOf(at.Row)),
                          NoticeTone::Warning, NoticeClockNow());
    else
        (void)sheet.Set(sections, at.Row, carried);
}

void SheetPanels::PickAsset(SheetPoint at, bool follows)
{
    // An asset is chosen from the shelf, among what the field can hold; with
    // Ctrl, the shelf shows the one it holds.
    const std::optional<UiRow> shown = Shown(at);
    const std::string current = shown.has_value() ? shown->Value : std::string{};
    if (follows && !current.empty())
    {
        Host.Assets.Reveal(current);
        return;
    }
    PropertySheet& sheet = Sheet(at.Sheet);
    Host.Assets.Pick({ .Title = sheet.LabelOf(at.Row),
                       .Only = sheet.CandidatesOf(Host.Sections(at.Sheet), at.Row),
                       .Current = current,
                       .OffersNone = true,
                       .Choose = [this, which = at.Sheet, key = sheet.EditKeyOf(at.Row)](const std::string& path,
                                                                                         bool commit) {
                           PropertySheet& chosen = Sheet(which);
                           const std::optional<std::size_t> row = chosen.IndexOf(key);
                           if (commit && row.has_value())
                               (void)chosen.Set(Host.Sections(which), *row, path);
                       } });
}

std::optional<std::size_t> SheetPanels::Present(SheetId sheet, std::span<const PropertySection> sections,
                                                bool everything)
{
    PublishTyping();
    PropertySheet& presented = Sheet(sheet);
    if (presented.Holding())
        return std::nullopt;
    const std::vector<UiRow> rows = presented.Present(sections, everything);
    (void)Host.Ui.SetRows(Screen, RowsOf(sheet), rows);
    return rows.size();
}

void SheetPanels::MovedOn(SheetId sheet)
{
    Sheet(sheet).Drop();
    if (Scrub.Holds() && Scrub.At().Sheet == sheet)
        Scrub.End(false);
}

void SheetPanels::PublishTyping()
{
    std::string typing;
    for (const SheetId sheet : { SheetId::Tool, SheetId::Selection })
        if (const std::optional<PropertySheet::TypedNumber> number = Sheet(sheet).Typing())
            typing = std::format("{}:{}:{}", SheetName(sheet), number->Row, number->Element);
    (void)Host.Ui.SetValue(Screen, TypingId, UiValue(std::move(typing)));
}
