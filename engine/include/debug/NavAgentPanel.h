#pragma once

#include <debug/IDebugPanel.h>

class NavAgentSystem;
class World;

// Where every navigation agent's order stands, and for the selected agent why:
// its route and cursor, the last search, its crossing, and its stuck window.
// Selecting also narrows nav.agent.debug.draw's on-top lines to that agent.
class NavAgentPanel : public IDebugPanel
{
public:
    NavAgentPanel(const World& world, NavAgentSystem& agents);

    void Draw() override;

private:
    const World& Entities;
    NavAgentSystem& Agents;
};
