#pragma once

#include "properties/PropertySource.h"
#include "workspaces/facets/SelectionFacet.h"

#include <ui/UiScreenDesc.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ActionCatalog;
class ActionContextStack;

//=============================================================================
// PropertySheet
// Property nodes as rows, and a row's edit back into its source by row index.
// A typed field holds the rows still (Hold) and commits once focus leaves
// (Release); the controls each kind gets are in editor/ARCHITECTURE.md.
//=============================================================================
class PropertySheet
{
public:
    // Where the actions a property names are labelled and run; without them
    // a property's actions are not offered.
    void OfferActions(const ActionCatalog& catalog, const ActionContextStack& actions);

    // `everything` includes properties marked advanced.
    [[nodiscard]] std::vector<UiRow> Present(const IPropertySource& source, bool everything = false);
    [[nodiscard]] std::vector<UiRow> Present(std::span<const PropertySection> sections, bool everything = false);

    // Row `index` set to `value` as one step.
    PropertyEditResult Set(IPropertySource& source, std::size_t index, std::string_view value);
    PropertyEditResult Set(std::span<const PropertySection> sections, std::size_t index, std::string_view value);
    // One number of a vector row set, the others keeping theirs.
    PropertyEditResult SetElement(std::span<const PropertySection> sections, std::size_t index, std::size_t element,
                                  std::string_view value);
    // The key an edit of row `index`, or of one of its numbers, is made by:
    // empty for a number its source has no key for.
    [[nodiscard]] std::string EditKeyOf(std::size_t index, std::optional<std::size_t> element = {}) const;
    // The bounds and step of row `index`, or of one of its numbers, as its
    // property describes them.
    [[nodiscard]] PropertyDescriptor BoundsOf(std::size_t index, std::optional<std::size_t> element = {}) const;
    [[nodiscard]] IPropertySource* SourceOf(std::span<const PropertySection> sections, std::size_t index) const;
    // Row `index` set to what its control reported: a drop-down's option, a
    // field's text.
    PropertyEditResult SetFromControl(IPropertySource& source, std::size_t index, const UiValue& reported);
    PropertyEditResult SetFromControl(std::span<const PropertySection> sections, std::size_t index,
                                      const UiValue& reported);
    // Row `index`'s verb or action `verb`, with `choice` when the verb asks for one.
    PropertyEditResult Act(IPropertySource& source, std::size_t index, std::string_view verb,
                           std::string_view choice = {});
    PropertyEditResult Act(std::span<const PropertySection> sections, std::size_t index, std::string_view verb,
                           std::string_view choice = {});
    // Row `index`'s verb, with what it asks to be chosen first; null when it has none.
    [[nodiscard]] const PropertyVerb* VerbOf(std::size_t index, std::string_view verb) const;
    // Whether row `index`'s verb takes the row's property away.
    [[nodiscard]] bool Removes(std::size_t index, std::string_view verb) const;
    [[nodiscard]] std::string LabelOf(std::size_t index) const;
    // The row presenting the property `key`, as last presented.
    [[nodiscard]] std::optional<std::size_t> IndexOf(std::string_view key) const;
    // A group row opened or closed.
    void Fold(std::size_t index);
    // What an asset row may be set to.
    [[nodiscard]] std::vector<std::string> CandidatesOf(std::span<const PropertySection> sections,
                                                        std::size_t index) const;
    // A toggle row turned the other way.
    PropertyEditResult Flip(IPropertySource& source, std::size_t index);
    PropertyEditResult Flip(std::span<const PropertySection> sections, std::size_t index);

    // Row `index`'s field took focus. Until it is released the rows must not
    // be presented again, or what is being typed would be replaced.
    void Hold(std::size_t index);
    // Row `index`'s number -- one of its vector's, or -1 for all of it -- shown
    // as a field to type into, held from now until it is let go.
    void Type(std::size_t index, std::int64_t element);
    [[nodiscard]] bool Holding() const { return Held.Kind != HoldKind::Nothing; }
    struct TypedNumber
    {
        std::size_t Row = 0;
        std::int64_t Element = -1;
    };
    [[nodiscard]] std::optional<TypedNumber> Typing() const;
    // The held field let go of focus holding `value`.
    PropertyEditResult Release(IPropertySource& source, std::size_t index, std::string_view value);
    PropertyEditResult Release(std::span<const PropertySection> sections, std::size_t index, std::string_view value);
    PropertyEditResult ReleaseElement(std::span<const PropertySection> sections, std::size_t index,
                                      std::size_t element, std::string_view value);
    // The held field went away without letting go -- its document closed, or
    // what it described did: nothing is committed.
    void Drop() { Held = {}; }

private:

    struct Presented
    {
        std::string Key;
        std::string Value;
        std::size_t Section = 0;
        std::string Label = {};
        std::vector<PropertyVerb> Verbs = {};
        std::vector<std::string> Actions = {};
        bool Foldable = false;
        bool Folded = false;
        // A vector's numbers' own keys, where its source has them.
        std::vector<std::string> ElementKeys = {};
        PropertyDescriptor Bounds = {};
        PropertyDescriptor ElementBounds = {};
        // What its control was laid out as: its key, verb and options.
        std::string Layout = {};
    };

    void Append(const PropertyNode& node, int depth, std::size_t section, bool everything, std::vector<UiRow>& rows);
    void AppendActions(const PropertyNode& node, int depth, std::size_t section, std::vector<UiRow>& rows);
    // Notes whether this presentation laid out other properties than the last.
    void Settle();

    [[nodiscard]] bool FoldedNow(const PropertyNode& node) const;

    std::vector<Presented> Rows;
    enum class HoldKind : std::uint8_t { Nothing, Field, Number };
    struct HeldRow
    {
        HoldKind Kind = HoldKind::Nothing;
        std::size_t Row = 0;
        std::int64_t Element = -1;
    };
    HeldRow Held;
    const ActionCatalog* Catalog = nullptr;
    const ActionContextStack* Actions = nullptr;
    // Groups opened or closed by hand, by key, against how each starts.
    std::unordered_map<std::string, bool> Folds;
    // Laid out anew -- other rows, or a row's other options: until it is
    // presented unchanged once, what its controls report is them settling on
    // their new values, not a person changing one.
    std::vector<std::string> LaidOut;
    bool Settling = false;
};
