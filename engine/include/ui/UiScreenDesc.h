#pragma once

#include <core/identity/StrongId.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiValue.h>

#include <cstdint>
#include <string>
#include <vector>

//=============================================================================
// What a screen presents and what it can ask for.
//
// Both halves are declared when the screen opens, not discovered as it runs.
// The document engine binds a data model before it parses the document that
// reads it, so the property set has to exist first -- and an action the host
// never declared is one it has no code to handle, which is better refused at
// open than dispatched at runtime.
//=============================================================================

// A property's identity within one screen. Stable for a given description, so a
// host resolves it once (or names it by the index it declared) and never parses
// a path again per frame.
using UiModelPropertyId = StrongId<struct UiModelPropertyTag, std::uint32_t>;

// A semantic action, likewise scoped to the screen that declares it. There is
// deliberately no global enum of every action in the product: actions belong to
// the feature that raises and handles them, and a pause menu's vocabulary is
// nothing the inspector should be able to name.
using UiActionId = StrongId<struct UiActionTag, std::uint32_t>;

// A list a document repeats over. Separate from a property because it is bound
// by address rather than through a value getter, and because a list is the one
// presentation shape whose size is part of what changed.
using UiModelArrayId = StrongId<struct UiModelArrayTag, std::uint32_t>;

// A list of labelled rows, which is the shape most editor surfaces actually
// present: an inspector's fields, a search result set, a property sheet. Kept
// distinct from a list of strings because a row has parts a document addresses
// separately -- and because the value part is editable where a plain list is
// not.
using UiModelRowsId = StrongId<struct UiModelRowsTag, std::uint32_t>;

struct UiModelProperty
{
    // The name the document reads, as written in the markup.
    std::string Path;

    // What the property reads before the host publishes anything. A screen that
    // opens before its first update still has to present something.
    UiValue Initial;

    // Whether a control may write this back -- a text field, a slider, a
    // checkbox bound to it.
    //
    // A write changes the presentation copy and nothing else. It does not reach
    // the application, and it is not a commit: the value a user is part-way
    // through typing is presentation state, exactly like a scroll offset. The
    // host learns what was typed by reading it back when the document raises
    // the action that says to -- an apply, a preview, a confirm -- and remains
    // free to validate it, transform it, or refuse it outright.
    //
    // That is what keeps undo, transactions, validation and scripting outside
    // the presentation layer. A control that committed on change would put them
    // all behind a keystroke.
    bool Editable = false;
};

// Which control a document offers for a row's value, when it offers one.
//
// Editable still says whether it offers one at all; this says which. A Range
// that is not editable is a read-out with a bar, and a document tests both.
// Text is the default and is what every existing row is.
enum class UiRowControl : std::uint8_t
{
    Text,
    Range,
    Choice,
};

// One row of a presented list.
//
// Strings, and only strings, for the same reason a list is: presentation is
// text. A number being edited is the text somebody is typing, and the host
// parses it when it reads the row back -- which is also where it gets to refuse
// "1.2.3" without the document ever having had an opinion. A slider bound to
// the value writes it back as text too, which is the same rule met from the
// other side.
struct UiRow
{
    std::string Label;
    std::string Value;
    // Extra text the document can show without it being another row: a unit, a
    // type name, a validation hint.
    std::string Detail;

    // Whether the document should offer a control for this row's value.
    //
    // Presentation metadata rather than a gate: a struct member is bound once
    // for the whole array, so `value` is read-write on every row and a document
    // that offers a control anyway can still write the copy. Nothing is lost by
    // that -- a write reaches the presentation copy and stops there, and the
    // host is still what decides whether a value read back becomes a change.
    // This is how a row says which of its values are worth offering: an
    // identity, a type it cannot express, a field it has no editor for.
    bool Editable = false;

    UiRowControl Control = UiRowControl::Text;

    // Range: the value as a number, and the bounds and step the control moves
    // within. Its own member rather than a reading of Value, because a document
    // that offers every control and shows one still binds them all -- a hidden
    // control is not an unbound one -- and two controls writing one variable
    // fight over it. A slider writes Number; a drop-down and a field write
    // Value; nothing has two writers.
    double Number = 0.0;
    double Min = 0.0;
    double Max = 1.0;
    double Step = 0.1;

    // Choice: the labels offered. Value is one of them -- a host that cannot
    // map its current value to a label puts the raw value here as well, so the
    // control can represent it and publishing it changes nothing. A drop-down
    // whose value matches no option selects the first and reports that as a
    // change, which is how "not offered" would otherwise become "rewritten".
    // Initialised explicitly so an aggregate that names only the first few
    // members -- which is how every existing row is written -- stays warning-
    // free: the compiler waives the missing-initializer warning for members
    // that have a default of their own.
    std::vector<std::string> Choices = {};

    friend bool operator==(const UiRow&, const UiRow&) = default;
};

struct UiScreenDesc
{
    // The cooked package, as an "asset://..." virtual path.
    std::string PackagePath;

    // The data model the document names in its `data-model` attribute. Empty
    // means the screen presents nothing and declares no actions -- a static
    // document, which is a legitimate thing to open.
    std::string ModelName;

    std::vector<UiModelProperty> Properties;

    // Lists the document repeats over with `data-for`. Strings, deliberately:
    // a presentation list is labels -- profile names, asset paths, search
    // results -- and a row needing more structure than that is a design
    // question rather than a missing overload.
    std::vector<std::string> Arrays;

    // Row lists the document repeats over. Each row's `value` is bound
    // read-write, so a bound control edits the presentation copy and the host
    // reads it back on an explicit action -- the same rule as an editable
    // property, for the same reason.
    std::vector<std::string> RowLists;

    // Action names as the document raises them, e.g. a `data-event-click`
    // calling "pause_activate". Ids are the index into this list plus one, so a
    // host can name them as constants beside the description that declares them.
    //
    // Identifiers, not dotted paths: a data expression reads `pause.resume` as
    // the member `resume` of something called `pause`, and binding it silently
    // does nothing. The runtime refuses a name it cannot bind, at open.
    std::vector<std::string> Actions;

    // A modal screen takes UI focus from the screens below it: they stop
    // receiving input while it is open.
    //
    // It says nothing about the application. Whether the game pauses, and
    // whether an editor's viewport tools keep working, are decisions their
    // hosts make with an InputContextLease -- this layer arbitrates presentation
    // focus and has no opinion on what a modal means to the thing behind it.
    bool Modal = false;
};

// Ids are positional, and both directions are spelled out so a caller never has
// to remember which way the plus-one goes.
[[nodiscard]] inline UiModelPropertyId UiPropertyIdAt(std::size_t index)
{
    return UiModelPropertyId{ static_cast<std::uint32_t>(index + 1) };
}

[[nodiscard]] inline UiActionId UiActionIdAt(std::size_t index)
{
    return UiActionId{ static_cast<std::uint32_t>(index + 1) };
}

[[nodiscard]] inline UiModelArrayId UiArrayIdAt(std::size_t index)
{
    return UiModelArrayId{ static_cast<std::uint32_t>(index + 1) };
}

[[nodiscard]] inline UiModelRowsId UiRowsIdAt(std::size_t index)
{
    return UiModelRowsId{ static_cast<std::uint32_t>(index + 1) };
}
