#include "shell/CommandPalette.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "text/TextFilterMatch.h"

#include <ui/UiKeys.h>
#include <ui/UiService.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace
{
constexpr std::size_t kShownAtMost = 12;

const UiModelPropertyId kQuery = UiPropertyIdAt(0);
const UiModelRowsId kResults = UiRowsIdAt(0);
const UiActionId kQueryAction = UiActionIdAt(0);
const UiActionId kKeyAction = UiActionIdAt(1);
const UiActionId kChooseAction = UiActionIdAt(2);
const UiActionId kCloseAction = UiActionIdAt(3);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://palette.rml";
    desc.ModelName = "palette";
    desc.Modal = true;
    desc.Properties = { UiModelProperty{ "query", UiValue(std::string{}), true } };
    desc.RowLists = { "results" };
    desc.Actions = { "palette_query", "palette_key", "palette_choose", "palette_close" };
    return desc;
}
} // namespace

std::vector<std::size_t> RankCommands(std::span<const CommandCandidate> candidates, std::string_view query,
                                      std::size_t limit)
{
    struct Scored
    {
        std::size_t Index;
        int Rank;
    };
    std::vector<Scored> scored;
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        if (const std::optional<int> label = TextFilterRank(candidates[i].Label, query))
            scored.push_back({ i, *label });
        else if (const std::optional<int> category = TextFilterRank(candidates[i].Category, query))
            scored.push_back({ i, 3 + *category });
    }
    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.Rank < b.Rank; });
    std::vector<std::size_t> ranked;
    for (std::size_t i = 0; i < scored.size() && i < limit; ++i)
        ranked.push_back(scored[i].Index);
    return ranked;
}

CommandPalette::CommandPalette(UiService& ui, UiSurfaceId surface, const ActionCatalog& catalog,
                               ActionContextStack& actions)
    : Ui(ui)
    , Catalog(catalog)
    , Actions(actions)
    , Screen(ui, Describe, surface)
{
}

CommandPalette::~CommandPalette()
{
    Close();
}

void CommandPalette::Open()
{
    if (IsOpen())
        return;
    (void)Screen.Open();
    Query.clear();
    Highlight = 0;
    Gather();
}

void CommandPalette::Close()
{
    Screen.Close();
}

void CommandPalette::Gather()
{
    // What this window can do at the moment it opened. Actions nothing claims
    // here are not offered: the palette is for doing, not for browsing a list
    // of things that would do nothing.
    Candidates.clear();
    for (const ActionDefinition* action : Catalog.All())
    {
        const ActionContextStack::State state = Actions.Query(action->Id);
        if (!state.Claimed || !state.Enabled)
            continue;
        Candidates.push_back({ action->Id, action->Label, action->Category, Catalog.ChordText(action->Id) });
    }
}

void CommandPalette::Update()
{
    if (!IsOpen())
        return;
    for (const UiAction& action : Ui.DrainActions(Screen.Handle()))
    {
        if (action.Id == kQueryAction)
        {
            Query = std::string(action.Arg(0).AsString());
            Highlight = 0;
            // A single-line field reports Enter as a change with a line break.
            if (action.Arg(1).AsBool())
            {
                Perform(Highlight);
                return;
            }
        }
        else if (action.Id == kKeyAction)
        {
            const std::optional<UiKey> key =
                UiKeyFromEvent(action.Arg(0).AsInt());
            if (key == UiKey::Escape)
            {
                Close();
                return;
            }
            if (key == UiKey::Down && Highlight + 1 < Shown.size())
                ++Highlight;
            else if (key == UiKey::Up && Highlight > 0)
                --Highlight;
        }
        else if (action.Id == kChooseAction)
        {
            Perform(static_cast<std::size_t>(std::max<std::int64_t>(0, action.Arg(0).AsInt())));
            return;
        }
        else if (action.Id == kCloseAction)
        {
            Close();
            return;
        }
    }
    Publish();
}

void CommandPalette::Publish()
{
    Shown = RankCommands(Candidates, Query, kShownAtMost);
    Highlight = Shown.empty() ? 0 : std::min(Highlight, Shown.size() - 1);
    std::vector<UiRow> rows;
    rows.reserve(Shown.size());
    for (std::size_t i = 0; i < Shown.size(); ++i)
    {
        const CommandCandidate& candidate = Candidates[Shown[i]];
        UiRow row{ candidate.Label, candidate.Chord, candidate.Category };
        row.Key = candidate.Key;
        row.Selected = i == Highlight;
        rows.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen.Handle(), kResults, rows);
}

void CommandPalette::Perform(std::size_t shownIndex)
{
    Publish();
    if (shownIndex >= Shown.size())
        return;
    const std::string key = Candidates[Shown[shownIndex]].Key;
    Close();
    (void)Actions.Invoke(key);
}
