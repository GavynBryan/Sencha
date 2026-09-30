#pragma once

#include "properties/PropertySource.h"
#include "workspaces/WorkspaceFacets.h"

#include <ui/UiScreenDesc.h>

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// PropertySheet
//
// Property nodes as the rows a document presents, and a presented row's edit
// back into the source it came from, by the row's index. A group becomes a
// section row over its children, as does each titled section of several
// sources; what a property is decides the control -- a choice a drop-down, a
// bounded number a slider, a switch a toggle, anything else a field.
//
// A field is typed into over many frames and edited once, when it lets go of
// focus: Hold stops the rows from changing under it, and Release commits what
// it holds. The sheet remembers which key and section each row was, from the
// last time it presented.
//=============================================================================
class PropertySheet
{
public:
    // `everything` includes properties marked advanced.
    [[nodiscard]] std::vector<UiRow> Present(const IPropertySource& source, bool everything = false);
    [[nodiscard]] std::vector<UiRow> Present(std::span<const PropertySection> sections, bool everything = false);

    // Row `index` set to `value` as one step.
    PropertyEditResult Set(IPropertySource& source, std::size_t index, std::string_view value);
    PropertyEditResult Set(std::span<const PropertySection> sections, std::size_t index, std::string_view value);
    // Row `index` set to what its control reported: a slider's number, a
    // drop-down's option, a field's text.
    PropertyEditResult SetFromControl(IPropertySource& source, std::size_t index, const UiValue& reported);
    PropertyEditResult SetFromControl(std::span<const PropertySection> sections, std::size_t index,
                                      const UiValue& reported);
    // A toggle row turned the other way.
    PropertyEditResult Flip(IPropertySource& source, std::size_t index);
    PropertyEditResult Flip(std::span<const PropertySection> sections, std::size_t index);

    // Row `index`'s field took focus. Until it is released the rows must not
    // be presented again, or what is being typed would be replaced.
    void Hold(std::size_t index);
    [[nodiscard]] bool Holding() const { return Held != kNothingHeld; }
    // The held field let go of focus holding `value`.
    PropertyEditResult Release(IPropertySource& source, std::size_t index, std::string_view value);
    PropertyEditResult Release(std::span<const PropertySection> sections, std::size_t index, std::string_view value);
    // The held field went away without letting go -- its document closed, or
    // what it described did: nothing is committed.
    void Drop() { Held = kNothingHeld; }

private:
    static constexpr std::size_t kNothingHeld = std::numeric_limits<std::size_t>::max();

    struct Presented
    {
        std::string Key;
        std::string Value;
        std::size_t Section = 0;
    };

    void Append(const PropertyNode& node, int depth, std::size_t section, bool everything, std::vector<UiRow>& rows);
    [[nodiscard]] IPropertySource* SourceOf(std::span<const PropertySection> sections, std::size_t index) const;

    std::vector<Presented> Rows;
    std::size_t Held = kNothingHeld;
};
