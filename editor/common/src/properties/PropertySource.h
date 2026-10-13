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
    // A number moved by `delta` from what BeginEdit found, rather than set: a
    // property standing for several values moves each from its own. Previewed,
    // or committed as one step, like a value; a source whose properties each
    // hold one value need not offer it.
    virtual PropertyEditResult PreviewEditBy(std::string_view /*key*/, double /*delta*/)
    {
        return PropertyEditResult::Refused;
    }
    virtual PropertyEditResult CommitEditBy(std::string_view /*key*/, double /*delta*/)
    {
        return PropertyEditResult::Refused;
    }
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

// A source that only shows, and acts through its verbs: no property of it is
// edited as a value.
class ReadOnlyPropertySource : public IPropertySource
{
public:
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Refused; }
    void CancelEdit(std::string_view) override {}
};

// A source whose values change only when committed -- a choice made, a field
// typed and entered -- so nothing previews and nothing is left to put back. An
// edit begins on any property its nodes show as writable.
class CommitOnlyPropertySource : public IPropertySource
{
public:
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    void CancelEdit(std::string_view) override {}
};
