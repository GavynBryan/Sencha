#pragma once

#include <cstdint>

class World;
class AssetSystem;
class JsonValue;

//=============================================================================
// ScriptSceneLink
//
// Links a scene's T script modules before its entities are created.
// LinkScriptsForScene runs in the zone BUILD phase (before any entity exists):
// it reads the scene's top-level `script_modules` manifest, loads and links
// each module (registering the module's script components, which the
// before-first-entity rule requires), and records each handle in the world's
// ScriptRuntime.
//
// The manifest is explicit rather than scanned from authored components: a
// `system` may iterate only native components and leave no script-component
// footprint in the scene. Once linked, a module's `system`/`observer`
// declarations run globally over the entities the finalize pass creates; a
// `behavior`'s authored config component is its per-entity attach point.
//=============================================================================

// Pre-entity link of a scene's scripts. Creates the ScriptRuntime resource if
// absent. `sceneJson` is the parsed scene root; its top-level `script_modules`
// array lists the module asset paths to link.
void LinkScriptsForScene(World& world, AssetSystem& assets, const JsonValue& sceneJson,
                         std::uint64_t worldSeed = 0);
