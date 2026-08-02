#pragma once

class Game;
struct RuntimeAssets;

// Binds a module's structured data registrations into one concrete asset host.
// The caller owns ordering and must unregister before the module is unmapped or
// the RuntimeAssets instance is destroyed.
void RegisterGameDataAssets(Game& game, RuntimeAssets& assets);
void UnregisterGameDataAssets(Game& game, RuntimeAssets& assets);
