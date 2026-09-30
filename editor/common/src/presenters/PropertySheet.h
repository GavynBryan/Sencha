#pragma once

#include "properties/PropertySource.h"

#include <ui/UiScreenDesc.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// PropertySheet
//
// Property nodes as the rows a document presents, and a presented row's edit
// back into the source it came from, by the row's index. A group becomes a
// section row over its children; what a property is decides the control -- a
// choice a drop-down, a bounded number a slider, a switch a toggle. The sheet
// remembers only which key each row was, from the last time it presented.
//=============================================================================
class PropertySheet
{
public:
    // `everything` includes properties marked advanced.
    [[nodiscard]] std::vector<UiRow> Present(const IPropertySource& source, bool everything = false);

    // Row `index` set to `value` as one step.
    PropertyEditResult Set(IPropertySource& source, std::size_t index, std::string_view value);
    // Row `index` set to what its control reported: a slider's number, a
    // drop-down's option, a field's text.
    PropertyEditResult SetFromControl(IPropertySource& source, std::size_t index, const UiValue& reported);
    // A toggle row turned the other way.
    PropertyEditResult Flip(IPropertySource& source, std::size_t index);

private:
    struct Presented
    {
        std::string Key;
        std::string Value;
    };

    void Append(const PropertyNode& node, int depth, bool everything, std::vector<UiRow>& rows);

    std::vector<Presented> Rows;
};
