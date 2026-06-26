#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

//=============================================================================
// EditorVisual
//
// Optional hint for how a tool (the editor) should visualize an entity that has
// a given component — e.g. a camera drawn as a little camera mesh at its
// transform. Pure description: the runtime ignores it entirely; only tools read
// it, surfaced generically through IComponentSerializer so the editor never
// names a component type. It lives alongside a component's other metadata
// (TypeSchema, storage traits) via the ComponentEditorVisual<T> trait, so a
// game module declares its own components' visuals the same way the engine does.
//=============================================================================
struct EditorVisual
{
    enum class Kind : std::uint8_t
    {
        Mesh, // wireframe of a mesh asset, drawn at the entity transform
    };

    Kind VisualKind = Kind::Mesh;
    std::string_view AssetPath; // logical asset name; the editor resolves it
};

// Specialize for a component to give it an editor visual. Default: none.
template <typename Component>
struct ComponentEditorVisual
{
    static constexpr std::optional<EditorVisual> Value = std::nullopt;
};
