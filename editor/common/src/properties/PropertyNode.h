#pragma once

#include <core/assets/AssetRef.h>

#include <cstdint>
#include <string>
#include <vector>

// What kind of value a property holds, which decides what a presenter offers
// for it -- a field, a slider, a switch, a picker -- without deciding how any of
// them look.
enum class PropertyKind : std::uint8_t
{
    Group,        // holds other properties and no value of its own
    Text,
    Number,
    Toggle,
    Choice,
    Vector,       // several numbers edited together, "x, y, z"
    Color,
    Asset,
    Unsupported,  // shown for completeness, never edited
};

// Something that can be done to a property, rather than a value it can hold:
// remove a list entry, add one, reset an override. A verb with choices needs
// one chosen first -- which kind of modifier to add.
struct PropertyVerb
{
    std::string Id;
    std::string Label;
    std::vector<std::string> Choices = {};
    // Doing it takes the property away: a component removed, a list entry dropped.
    bool Removes = false;
    // Beside each choice, what choosing it brings with it; empty, or one per choice.
    std::vector<std::string> ChoiceNotes = {};
};

struct PropertyDescriptor
{
    PropertyKind Kind = PropertyKind::Text;
    std::string Label;
    std::string Tooltip = {};
    std::vector<std::string> Choices = {};
    AssetType Asset = AssetType::Unknown;
    // A number's range and the step it moves by; Max below Min is unbounded.
    double Min = 0.0;
    double Max = -1.0;
    double Step = 0.0;
    bool ReadOnly = false;
    // Text that runs over several lines: a description, a document's source.
    bool Multiline = false;
    // Kept out of a compact presentation until someone asks for everything.
    bool Advanced = false;
    // A group whose children are worth knowing about but not worth reading
    // every time: presented closed until opened.
    bool Folded = false;
    // Names a purpose-built editor that presents this property instead of the
    // generic one; empty for the generic one.
    std::string PresentationHint = {};
    std::vector<PropertyVerb> Verbs = {};
    // Catalog actions about what this property stands for -- a brush's bake and
    // export -- offered beside it, invoked through whatever claims them.
    std::vector<std::string> Actions = {};
};

// One property of whatever is being edited: a component field, a data field, a
// tool parameter. The key is the source's stable name for it and is what an
// edit comes back with. The value is text, the same rule the UI layer keeps:
// presentation is what somebody reads and types, and the source parses it.
struct PropertyNode
{
    std::string Key;
    PropertyDescriptor Descriptor;
    std::string Value = {};
    std::vector<PropertyNode> Children = {};
    // Stands for several values that differ -- the same field of several
    // selected objects -- so Value is empty: setting it sets them all alike,
    // and moving it by an amount moves each from where it is.
    bool Mixed = false;
};
