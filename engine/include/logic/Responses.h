#pragma once

#include <core/metadata/EditorVisual.h>
#include <ecs/ComponentAnnotations.h>
#include <logic/ResponseSetKey.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

// An entity's authored responses: the declared occurrences it answers and what
// each answer runs, named by a content key into the World's response sets, and
// the state each answer carries at runtime. An entity without authored
// responses carries none of this. docs/gameplay/responses.md.
//
// Bit i of each mask is binding i of the set, and a set holds its bindings in
// key order, so a bit always means the same authored key while the set stands.
// Changing the set carries each binding's bits across by key; see
// SetResponseBindings.

inline constexpr std::size_t kResponseBindingCapacity = 32;

struct SENCHA_COMPONENT("sencha.responses") Responses
{
    // The bindings, shared by every entity authored alike.
    ResponseSetKey Set{};
    // Bindings that do not respond: authored to start so, or switched off by
    // responses.disable.
    std::uint32_t Disabled = 0;
    // Once-bindings that have run.
    std::uint32_t Consumed = 0;
};

static_assert(std::is_trivially_copyable_v<Responses>);

#if !defined(SENCHA_CODEGEN)
#  include <logic/Responses.sencha.h>
#endif

// A volume with responses is a trigger volume, and draws orange in the editor,
// whatever else it is. Other entities with responses draw nothing for it.
template <>
struct ComponentEditorVisual<Responses>
{
    static constexpr std::optional<EditorVisual> Value = EditorVisual{
        .VisualKind = EditorVisual::Kind::VolumeTint,
        .AssetPath = {},
        .Fill = { 0.85f, 0.32f, 0.02f, 0.16f },
        .Edge = { 1.0f, 0.45f, 0.05f, 0.95f },
    };
};
