#pragma once

#include "document/WorldMap.h"

#include "ui/UiScreenSlot.h"

#include <cstddef>
#include <optional>
#include <string>

class UiService;

//=============================================================================
// WorldMapSurface
// The world map over the views: a plate per zone with its state, the docks and
// links between them, and the camera. Display only: the workspace hit-tests the
// map it laid out, and no screen is open while it is hidden.
//=============================================================================
class WorldMapSurface
{
public:
    explicit WorldMapSurface(UiService& ui);

    void MoveTo(UiSurfaceId content) { Screen.MoveTo(content); }
    // Shows `map` over `ground`, titled `title`, the plate of `hot` lit; a null
    // map closes it.
    void Present(const WorldMap* map, const Rect2d& ground, const std::string& title, std::optional<ZoneId> hot);

private:
    UiService& Ui;
    UiScreenSlot Screen;
};
