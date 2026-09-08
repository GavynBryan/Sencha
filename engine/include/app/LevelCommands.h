#pragma once

class ConsoleService;
class Engine;

// Registers the console commands that load and inspect a level: map, world,
// zone, zones, scene.spawn, scene.despawn.
//
// They are the engine's because loading a cooked level has one meaning for
// every game. Before this the engine owned the `map` name but not the ability
// to act on it, and asked a game through a handler it had to install first --
// which meant a process with no game, or a game that forgot, had a `map`
// command that could only report that nothing would answer it.
void RegisterLevelCommands(ConsoleService& console, Engine& engine);
