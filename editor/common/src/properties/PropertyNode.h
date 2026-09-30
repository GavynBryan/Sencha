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
    // Kept out of a compact presentation until someone asks for everything.
    bool Advanced = false;
    // Names a purpose-built editor that presents this property instead of the
    // generic one; empty for the generic one.
    std::string PresentationHint = {};
    std::vector<PropertyVerb> Verbs = {};
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
};
