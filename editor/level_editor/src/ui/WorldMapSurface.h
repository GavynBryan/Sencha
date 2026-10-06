#pragma once

#include "document/WorldMap.h"

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

#include <cstddef>
#include <optional>
#include <string>

class UiService;

//=============================================================================
// WorldMapSurface
//
// The world map on the window's content surface, over the views: a plate per
// zone -- the one being edited, the ones open beside it, the one the camera is
// in, the one under the pointer and the ones with problems each marked -- the
// docks and links between them, one-way ones with an arrow, and the camera. It
// only shows: the workspace hit-tests the map it laid out. No screen is open
// while the map is not shown.
//=============================================================================
class WorldMapSurface
{
public:
    explicit WorldMapSurface(UiService& ui);
    ~WorldMapSurface();

    WorldMapSurface(const WorldMapSurface&) = delete;
    WorldMapSurface& operator=(const WorldMapSurface&) = delete;

    void MoveTo(UiSurfaceId content);
    // Shows `map` over `ground`, titled `title`, the plate of `hot` lit; a null
    // map closes it.
    void Present(const WorldMap* map, const Rect2d& ground, const std::string& title, std::optional<ZoneId> hot);

private:
    void Open();
    void Close();

    UiService& Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
    std::size_t LongestZones = 0;
    std::size_t LongestLinks = 0;
    std::size_t LongestArrows = 0;
};
