#pragma once

#include <debug/IDebugPanel.h>

class PerceptionSystem;
class World;

// Why an observer did or did not see a target: last tick's counters, each
// observer's coverage, and the selected observer's candidates. Selecting also
// narrows perception.debug.draw to that observer.
class PerceptionPanel : public IDebugPanel
{
public:
    PerceptionPanel(const World& world, PerceptionSystem& perception);

    void Draw() override;

private:
    const World& Entities;
    PerceptionSystem& Perception;
};
