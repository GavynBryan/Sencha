#pragma once

#include "properties/PropertyNode.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class PropertyEditResult : std::uint8_t
{
    Applied,
    Unchanged,
    Refused,  // the value is not one the property can hold
    Gone,     // nothing answers to that key any more
};

//=============================================================================
// IPropertySource
//
// Something with properties, as a tree, and the one way they change: begin,
// preview, commit, cancel. A source maps that lifecycle onto its document's own
// transaction -- the command stack's pending edit for component bytes, a data
// document's staged edit -- so a presenter never writes authoring state itself
// and every live edit is revertible until it commits.
//=============================================================================
class IPropertySource
{
public:
    virtual ~IPropertySource() = default;

    [[nodiscard]] virtual std::vector<PropertyNode> Nodes() const = 0;

    // Opens an edit of one property, remembering what it holds now.
    virtual bool BeginEdit(std::string_view key) = 0;
    // Shows a value at once without it being a step yet: a slider mid-drag.
    virtual PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) = 0;
    // One undoable step from what BeginEdit found to this value.
    virtual PropertyEditResult CommitEdit(std::string_view key, std::string_view value) = 0;
    // Puts back what BeginEdit found.
    virtual void CancelEdit(std::string_view key) = 0;
    // What an asset property may be set to, as the values an edit takes. Asked
    // when a picker opens, never per frame: it may scan the project.
    [[nodiscard]] virtual std::vector<std::string> Candidates(std::string_view /*key*/) const { return {}; }
    // One of the property's verbs, as one step; `choice` is one of the verb's
    // choices, or empty for a verb that has none.
    virtual PropertyEditResult Act(std::string_view /*key*/, std::string_view /*verb*/, std::string_view /*choice*/)
    {
        return PropertyEditResult::Gone;
    }
};
