#pragma once

#include "ui/UiScreenSlot.h"

#include <ui/UiSurface.h>

#include <cstddef>

class RadialMenuSession;
class UiService;
struct IRadialMenuModel;

//=============================================================================
// RadialMenuScreen
//
// A radial menu drawn on an authored surface: a plate with a petal for each
// entry cut into it, a glyph in each petal at the slot the session lays it out
// at, the hot entry's variants as a fan of petals beyond the rim, and a
// caption naming what a release would choose. Display
// only -- the session hit-tests the same layout -- and one screen for every
// wheel a workspace has, since only one is ever open; its document is open
// only while one is.
//=============================================================================
class RadialMenuScreen
{
public:
    explicit RadialMenuScreen(UiService& ui);

    // The display-only surface over everything in the window; the screen
    // follows the workspace there. Invalid closes it.
    void MoveTo(UiSurfaceId overlay) { Screen.MoveTo(overlay); }
    void SetShown(bool shown);

    // Per frame: the open wheel, or none.
    void Present(const RadialMenuSession* wheel, const IRadialMenuModel* menu);

private:
    UiService& Ui;
    UiScreenSlot Screen;
    bool Shown = false;
};
