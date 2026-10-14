#pragma once

#include "ui/UiScreenSlot.h"

#include <ui/UiSurface.h>

#include <cstddef>

class RadialMenuSession;
class UiService;
struct IRadialMenuModel;

//=============================================================================
// RadialMenuScreen
// A radial menu on an authored surface, display only: the session hit-tests the
// same layout. One screen serves every wheel, and is open only while one is.
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
