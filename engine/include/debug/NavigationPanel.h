#pragma once

#include <debug/IDebugPanel.h>

class NavigationSystem;
class RuntimeWorld;

//=============================================================================
// NavigationPanel
//
// Answers "is navigation loaded, and what state are its links in?" for the
// running game: per resident zone, each build profile's tiles and polygons,
// each link's traversal kind, enabled state, cost scale, and revision, the
// names that failed to bind, and the last link-state tick. Text only; it draws
// nothing into the world.
//=============================================================================
class NavigationPanel : public IDebugPanel
{
public:
    NavigationPanel(const RuntimeWorld& world, const NavigationSystem& navigation);

    void Draw() override;

private:
    const RuntimeWorld& World;
    const NavigationSystem& Navigation;
};
