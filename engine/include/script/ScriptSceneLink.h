#pragma once

#include <cstdint>

class World;
class AssetSystem;
class JsonValue;
struct FixedLogicContext;

//=============================================================================
// ScriptSceneLink (docs/plans/t-language.md, milestone 5 integration)
//
// Connects authored ScriptSource components to the runtime bridges. Two steps
// bracket scene load:
//
//   LinkScriptsForScene runs in the zone BUILD phase, before any entity is
//   created: it scans the scene JSON for ScriptSource script paths, loads and
//   links each module (registering the module's script components, which the
//   before-first-entity rule requires), and records each handle in the
//   world's ScriptRuntime.
//
//   ScriptResolveSystem runs on the fixed tick after scene load: for each
//   entity carrying a ScriptSource whose handle maps to a linked module, it
//   attaches the runtime ScriptBehavior (and a cue buffer) that
//   ScriptBehaviorSystem ticks. Idempotent.
//=============================================================================

// Pre-entity link of a scene's scripts. Creates the ScriptRuntime resource if
// absent. `sceneJson` is the parsed scene root.
void LinkScriptsForScene(World& world, AssetSystem& assets, const JsonValue& sceneJson,
                         std::uint64_t worldSeed = 0);

class ScriptResolveSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);
};
