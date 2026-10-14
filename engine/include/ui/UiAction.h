#pragma once

#include <ui/UiScreenDesc.h>
#include <ui/UiScreenHandle.h>
#include <ui/UiValue.h>

#include <cstddef>
#include <vector>

//=============================================================================
// UiAction
//
// What a document asked for, as the host receives it.
//
// Everything here is a copy the host owns. No element pointer, no DOM node, no
// entity, no editor object, no callback the document registered -- a UI that
// could hand one of those back would be a UI that can reach into the thing it
// is supposed to be presenting.
//
// An action says what was asked, never how to do it. Interpreting
// "inspector.set_position" into a validated, undoable command is the editor
// controller's job, which is what keeps undo, transactions and scripting
// outside the presentation layer entirely.
//=============================================================================
struct UiAction
{
    // Which screen raised it. A host with several open tells them apart by
    // this rather than by inspecting the payload.
    UiScreenHandle Screen;

    // Index into that screen's declared action list, plus one.
    UiActionId Id;

    // Whatever the document passed along with it, in order. Copied and owned.
    std::vector<UiValue> Arguments;

    // Argument `index`, or no value when the document passed fewer.
    [[nodiscard]] const UiValue& Arg(std::size_t index) const
    {
        static const UiValue kNothing;
        return index < Arguments.size() ? Arguments[index] : kNothing;
    }
};
