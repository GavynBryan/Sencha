#pragma once

#include <cstdint>

class ConsoleRegistry;
class Engine;

//=============================================================================
// Net console commands
//
// host, connect, disconnect, and net_status: the surface that makes a session
// something a person can start and observe without a game module knowing
// anything about networking yet.
//
// Registered by the engine, because the session is an engine service. The
// identity these hand to the session comes from the engine and the loaded game
// module, so a player cannot type their way past the compatibility gate.
//=============================================================================
void RegisterNetConsoleCommands(ConsoleRegistry& registry, Engine& engine);
