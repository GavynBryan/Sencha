#pragma once

//=============================================================================
// GameplayTagContainer scene serialization
//
// Persists a GameplayTagContainer by tag *name*, resolved through the
// GameplayTagRegistry on load. Names are stable; ids are registration-order
// dependent and may differ per world, so persisting names lets a scene reload
// against any registry that knows the same vocabulary. Works over the generic
// archive interface (JSON in practice; see the binary note in the .cpp).
//=============================================================================

struct IWriteArchive;
struct IReadArchive;
struct GameplayTagContainer;
class GameplayTagRegistry;

bool WriteGameplayTags(IWriteArchive& archive,
                       const GameplayTagContainer& tags,
                       const GameplayTagRegistry& registry);

bool ReadGameplayTags(IReadArchive& archive,
                      GameplayTagContainer& tags,
                      const GameplayTagRegistry& registry);
