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

struct UiModelProperty
{
    // The name the document reads, as written in the markup -- "health" for
    // {{health}}, or "weapon.name" for a nested one.
    std::string Path;

    // What the property reads before the host publishes anything. A screen that
    // opens before its first update still has to present something.
    UiValue Initial;
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

    // Action names as the document raises them, e.g. a `data-event-click`
    // calling "pause.resume". Ids are the index into this list plus one, so a
    // host can name them as constants beside the description that declares them.
    std::vector<std::string> Actions;
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
