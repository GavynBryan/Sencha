#pragma once

class WorldComponentSchema;

// Adds every engine-owned component type that may appear in a runtime World.
//
// The order intentionally mirrors the current default zone initialization path:
// scene storage (including derived transform columns), physics, AbilityKit,
// movement, then camera runtime data. Keeping this prefix stable lets current
// registry Worlds and the future unified World coexist during migration.
void RegisterEngineRuntimeComponents(WorldComponentSchema& schema);
