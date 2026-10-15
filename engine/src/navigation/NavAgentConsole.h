#pragma once

class ConsoleRegistry;
class World;

// nav.agent.list, nav.agent.goto, nav.agent.follow and nav.agent.stop: give a
// navigation agent an order by hand and watch what becomes of it.
void RegisterNavigationAgentConsole(ConsoleRegistry& console, World& world);
