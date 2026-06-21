#pragma once

//=============================================================================
// ComponentRemovable
//
// Whether a tool (the editor) may remove a given component from an entity.
// Pure tooling metadata: the runtime ignores it; only the editor reads it,
// surfaced generically through IComponentSerializer so the editor never names
// a component type. Default: removable. A component opts out by specializing
// this trait to false, alongside its other metadata (TypeSchema, storage
// traits) the same way ComponentEditorVisual<T> works, so a game module
// declares its own components' policy the same way the engine does.
//
// LocalTransform opts out: it is structural (paired with the derived
// WorldTransform), so removing it would orphan that pairing.
//=============================================================================
template <typename Component>
struct ComponentRemovable
{
    static constexpr bool Value = true;
};
