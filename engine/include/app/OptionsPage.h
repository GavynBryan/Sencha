#pragma once

#include <ui/UiScreenDesc.h>
#include <ui/UiValue.h>

#include <cstdint>
#include <string>
#include <vector>

class ConsoleRegistry;

//=============================================================================
// OptionsPage
//
// The settings a player is offered, and what each one writes.
//
// A curated table rather than a listing of every archived cvar. The ~12 that
// carry CVarFlags::Archive today are engine tuning -- cadence-lock tolerance,
// baked-direct toggles -- and a page over all of them would be a developer
// panel wearing a player's name. So a row names a cvar, a label somebody would
// recognise, and the control that edits it.
//
// A row exists only if its cvar does. That is the whole of how a host offers
// what it can do: a headless host registers no window mode, a host with no
// mixer registers no volume, and this shows what it finds.
//
// The document owns the control; this owns what the control's value means. It
// holds no graphics, audio or input logic of its own -- it reads with the
// console's read helpers and writes with SetCVar, and what a row means lives in
// that cvar's own registration.
//=============================================================================

enum class OptionControl : std::uint8_t
{
    // A number between two bounds, moved in steps: a slider.
    Range,
    // One of a fixed set, each shown by a label: a drop-down.
    Choice,
};

// One entry of a Choice row: what the player reads, and what the cvar gets.
// "Unlimited" is a label; "0" is a value; the document only ever sees labels.
struct OptionChoice
{
    std::string Label;
    std::string Value;
};

struct OptionRow
{
    std::string Label;
    std::string CVar;
    OptionControl Control = OptionControl::Range;

    // Range: the step the control snaps to, and the bounds it moves within.
    double Step = 0.1;
    double Min = 0.0;
    double Max = 1.0;

    // Choice: the entries offered, in order.
    std::vector<OptionChoice> Choices;
};

class OptionsPage
{
public:
    // The engine's own table, filtered to the cvars this process registered.
    // A game replaces or extends it by editing Rows before the page opens.
    void InstallDefaults(const ConsoleRegistry& registry);

    [[nodiscard]] std::vector<OptionRow>& Rows() { return Rows_; }
    [[nodiscard]] const std::vector<OptionRow>& Rows() const { return Rows_; }

    // What the document presents: each row's label, the control it asks for,
    // and the value as the cvar stands now -- a Range snapped to its step, a
    // Choice as the label of its current entry. A Choice whose current value
    // has no entry is presented as itself, appended to the choices, so the
    // control can show it and publishing changes nothing.
    [[nodiscard]] std::vector<UiRow> Present(const ConsoleRegistry& registry) const;

    // The document changed a row's control, and this is the value it now
    // shows. A Range takes the number, snapped and clamped to the row; a Choice
    // takes a label and maps it to the value it stands for. Returns false
    // without writing when the value is not one for this row -- and when it is
    // exactly what Present would show for the cvar as it stands, because the
    // document engine raises a change on publish as well as on input, and
    // opening the page must be a fixed point.
    bool Apply(ConsoleRegistry& registry, std::size_t index, const UiValue& presented) const;

    // The page's own description, for the host that opens it.
    [[nodiscard]] UiScreenDesc Describe(std::string package) const;

private:
    std::vector<OptionRow> Rows_;
};
