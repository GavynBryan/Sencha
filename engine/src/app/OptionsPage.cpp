#include <app/OptionsPage.h>

#include <core/console/CVarRead.h>
#include <core/console/ConsoleRegistry.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <utility>

namespace
{
    // Enough digits to read back what a step produced, and not so many that a
    // volume reads as 0.7000000000000001.
    std::string FormatScalar(double value)
    {
        std::string text = std::to_string(value);
        // std::to_string gives six decimals; trim the ones that say nothing.
        while (text.size() > 1 && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
        return text;
    }

    // The value the row's control would land on: on a step, inside the bounds.
    double Snap(const OptionRow& row, double value)
    {
        if (row.Step > 0.0)
            value = row.Min + std::round((value - row.Min) / row.Step) * row.Step;
        return std::clamp(value, row.Min, row.Max);
    }

    std::optional<double> ParseNumber(std::string_view text)
    {
        if (text.empty())
            return std::nullopt;
        const std::string owned(text);
        char* end = nullptr;
        const double value = std::strtod(owned.c_str(), &end);
        if (end == owned.c_str() || *end != '\0')
            return std::nullopt;
        return value;
    }

    // Whether this entry is the cvar's current value. A string cvar compares
    // as text; a numeric one as a number, so "144" matches 144.0 however the
    // console happens to print it.
    bool Matches(const ConsoleRegistry& registry, const CVarMetadata& meta,
                 const OptionRow& row, const OptionChoice& choice)
    {
        if (meta.Type == CVarType::String)
            return ReadCVarString(&registry, row.CVar, "") == choice.Value;
        if (meta.Type == CVarType::Bool)
            return ParseCVarValue(meta.Type, choice.Value) == std::optional<CVarValue>(meta.CurrentValue);
        const std::optional<double> wanted = ParseNumber(choice.Value);
        return wanted.has_value()
            && std::abs(*wanted - ReadCVarDouble(&registry, row.CVar, 0.0)) < 1e-9;
    }

    // The current value as text, for when no entry stands for it.
    std::string RawCurrent(const ConsoleRegistry& registry, const CVarMetadata& meta,
                           const OptionRow& row)
    {
        if (meta.Type == CVarType::String)
            return ReadCVarString(&registry, row.CVar, "");
        return FormatScalar(ReadCVarDouble(&registry, row.CVar, 0.0));
    }

    const OptionChoice* CurrentChoice(const ConsoleRegistry& registry, const CVarMetadata& meta,
                                      const OptionRow& row)
    {
        for (const OptionChoice& choice : row.Choices)
            if (Matches(registry, meta, row, choice))
                return &choice;
        return nullptr;
    }

    // A slider reports a number and only a number. A document that offers
    // every control and shows one still has the others bound, and each reports
    // once as it receives its first value -- so the kind of what arrives is
    // what says which control spoke, and a string arriving for a range is the
    // hidden drop-down echoing, not the player.
    std::optional<double> NumberOf(const UiValue& value)
    {
        switch (value.Kind())
        {
        case UiValueKind::Float:
        case UiValueKind::Int:
            return value.AsFloat();
        default:
            return std::nullopt;
        }
    }

    const CVarMetadata* EditableCVar(const ConsoleRegistry& registry, const OptionRow& row)
    {
        const CVarMetadata* meta = registry.FindCVar(row.CVar);
        return meta != nullptr && OptionControlSuits(row.Control, meta->Type) ? meta : nullptr;
    }

    std::string_view ToggleLabel(bool on)
    {
        return on ? kOptionToggleOn : kOptionToggleOff;
    }
}

bool OptionControlSuits(OptionControl control, CVarType type)
{
    switch (control)
    {
    case OptionControl::Range:
        return type == CVarType::Int || type == CVarType::Double;
    case OptionControl::Choice:
        return true;
    case OptionControl::Toggle:
        return type == CVarType::Bool;
    }
    return false;
}

void OptionsPage::InstallDefaults(const ConsoleRegistry& registry)
{
    // Five settings, and they are the five a player looks for. Deliberately not
    // every archived cvar: the rest are developer tuning, and a page that
    // listed them would be a different thing wearing this one's name.
    //
    // The frame cap is a list of caps rather than a slider whose leftmost
    // position means "fastest": the value 0 is what the cvar calls no cap, and
    // a label is what a player calls it.
    const OptionRow candidates[] = {
        OptionRow{ .Label = "Master Volume",    .CVar = "audio.volume",
                   .Control = OptionControl::Range, .Step = 0.05, .Min = 0.0, .Max = 1.0,
                   .Choices = {} },
        OptionRow{ .Label = "Look Sensitivity", .CVar = "input.look_sensitivity",
                   .Control = OptionControl::Range, .Step = 0.05, .Min = 0.25, .Max = 3.0,
                   .Choices = {} },
        OptionRow{ .Label = "Display Mode",     .CVar = "window.mode",
                   .Control = OptionControl::Choice, .Step = 0.0, .Min = 0.0, .Max = 0.0,
                   .Choices = { { "Windowed", "windowed" }, { "Borderless", "borderless" },
                                { "Fullscreen", "fullscreen" } } },
        OptionRow{ .Label = "Frame Rate Cap",   .CVar = "r.target_fps",
                   .Control = OptionControl::Choice, .Step = 0.0, .Min = 0.0, .Max = 0.0,
                   .Choices = { { "Unlimited", "0" }, { "30", "30" }, { "60", "60" },
                                { "90", "90" }, { "120", "120" }, { "144", "144" },
                                { "240", "240" } } },
        OptionRow{ .Label = "Reduced Flashing", .CVar = "render.reduced_flashing",
                   .Control = OptionControl::Toggle, .Step = 0.0, .Min = 0.0, .Max = 0.0,
                   .Choices = {} },
    };

    std::vector<OptionRow> offered;
    for (const OptionRow& row : candidates)
    {
        // The row exists exactly when the thing behind it does.
        if (registry.FindCVar(row.CVar) != nullptr)
            offered.push_back(row);
    }
    RegisterRows(kEngineOwner, std::move(offered));
}

void OptionsPage::RegisterRows(std::string_view owner, std::vector<OptionRow> rows)
{
    auto run = std::ranges::find(Runs, owner, &OwnedRun::Owner);
    if (run == Runs.end())
        run = Runs.insert(run, OwnedRun{ .Owner = std::string(owner), .Count = 0 });
    Rows_.insert(EraseRowsOf(*run), std::make_move_iterator(rows.begin()), std::make_move_iterator(rows.end()));
    run->Count = rows.size();
}

void OptionsPage::UnregisterOwner(std::string_view owner)
{
    const auto run = std::ranges::find(Runs, owner, &OwnedRun::Owner);
    if (run == Runs.end())
        return;
    (void)EraseRowsOf(*run);
    Runs.erase(run);
}

std::vector<OptionRow>::iterator OptionsPage::EraseRowsOf(const OwnedRun& run)
{
    std::size_t offset = 0;
    for (const OwnedRun* earlier = Runs.data(); earlier != &run; ++earlier)
        offset += earlier->Count;
    const auto first = Rows_.begin() + static_cast<std::ptrdiff_t>(offset);
    return Rows_.erase(first, first + static_cast<std::ptrdiff_t>(run.Count));
}

std::vector<UiRow> OptionsPage::Present(const ConsoleRegistry& registry) const
{
    std::vector<UiRow> presented;
    presented.reserve(Rows_.size());

    for (const OptionRow& row : Rows_)
    {
        const CVarMetadata* meta = EditableCVar(registry, row);
        UiRow out;
        out.Label = row.Label;
        if (meta == nullptr)
        {
            presented.push_back(std::move(out));
            continue;
        }
        out.Editable = true;

        if (row.Control == OptionControl::Range)
        {
            out.Control = UiRowControls::Range;
            out.Min = row.Min;
            out.Max = row.Max;
            out.Step = row.Step;
            // Snapped here so the control receives a value already on its
            // step: what it then reports back is what it was given. The text
            // beside it is the read-out a document shows when it offers no
            // control.
            out.Number = Snap(row, ReadCVarDouble(&registry, row.CVar, row.Min));
            out.Value = FormatScalar(out.Number);
        }
        else if (row.Control == OptionControl::Toggle)
        {
            out.Control = UiRowControls::Toggle;
            out.Choices = { std::string(kOptionToggleOff), std::string(kOptionToggleOn) };
            out.Value = ToggleLabel(ReadCVarBool(&registry, row.CVar, false));
        }
        else
        {
            out.Control = UiRowControls::Choice;
            for (const OptionChoice& choice : row.Choices)
                out.Choices.push_back(choice.Label);
            if (const OptionChoice* current = CurrentChoice(registry, *meta, row))
            {
                out.Value = current->Label;
            }
            else
            {
                // Preserved structurally: an option that is the value itself,
                // so the control can select it rather than fall back to the
                // first entry and report that as the player's choice.
                out.Value = RawCurrent(registry, *meta, row);
                out.Choices.push_back(out.Value);
            }
        }
        presented.push_back(std::move(out));
    }
    return presented;
}

bool OptionsPage::Apply(ConsoleRegistry& registry, std::size_t index,
                        const UiValue& presented) const
{
    if (index >= Rows_.size())
        return false;
    const OptionRow& row = Rows_[index];
    const CVarMetadata* meta = EditableCVar(registry, row);
    if (meta == nullptr)
        return false;

    if (row.Control == OptionControl::Range)
    {
        const std::optional<double> incoming = NumberOf(presented);
        if (!incoming.has_value())
            return false;
        const double next = Snap(row, *incoming);
        const double shown = Snap(row, ReadCVarDouble(&registry, row.CVar, row.Min));
        // The control reports what it shows, and what it shows is what
        // Present gave it. Equal means nothing was asked for.
        if (std::abs(next - shown) < 1e-9)
            return false;
        // An integer cvar takes an integer, whatever the step arithmetic
        // produced on the way.
        const CVarValue value = meta->Type == CVarType::Int
            ? CVarValue{ static_cast<std::int64_t>(std::llround(next)) }
            : CVarValue{ next };
        return registry.SetCVar(row.CVar, value, { "options" },
                                ConsolePhase::EngineReady).Succeeded();
    }

    // And a drop-down reports a label and only a label; a number arriving for
    // a choice is the hidden slider echoing its own publish.
    if (presented.Kind() != UiValueKind::String)
        return false;
    const std::string label(presented.AsString());

    if (row.Control == OptionControl::Toggle)
    {
        if (label != kOptionToggleOn && label != kOptionToggleOff)
            return false;
        const bool next = label == kOptionToggleOn;
        if (next == ReadCVarBool(&registry, row.CVar, false))
            return false;
        return registry.SetCVar(row.CVar, CVarValue{ next }, { "options" },
                                ConsolePhase::EngineReady).Succeeded();
    }
    const OptionChoice* current = CurrentChoice(registry, *meta, row);
    const auto it = std::find_if(row.Choices.begin(), row.Choices.end(),
                                 [&](const OptionChoice& c) { return c.Label == label; });
    if (it == row.Choices.end())
    {
        // The one label that is not an entry: the current value, presented as
        // itself because nothing stood for it. Choosing it again is a no-op,
        // and anything else is not a value for this row.
        return false;
    }
    if (current == &*it)
        return false;

    const std::optional<CVarValue> value = ParseCVarValue(meta->Type, it->Value);
    if (!value.has_value())
        return false;
    return registry.SetCVar(row.CVar, *value, { "options" },
                            ConsolePhase::EngineReady).Succeeded();
}

UiScreenDesc OptionsPage::Describe(std::string package) const
{
    UiScreenDesc desc;
    desc.PackagePath = std::move(package);
    // Its own name: a context holds one data model per name, so a page opened
    // over the pause root cannot reuse the root's.
    desc.ModelName = "options";
    desc.Modal = true;
    desc.Properties = {
        UiModelProperty{ "title", UiValue(std::string("Options")) },
        UiModelProperty{ "hint", UiValue(std::string("Select a setting to change it.")) },
    };
    desc.RowLists = { "rows" };
    desc.Actions = { "options_activate" };
    return desc;
}
