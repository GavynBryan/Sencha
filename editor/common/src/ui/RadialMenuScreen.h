#pragma once

#include <ui/UiScreenHandle.h>
#include <ui/UiSurface.h>

class RadialMenuSession;
class UiService;
struct IRadialMenuModel;

//=============================================================================
// RadialMenuScreen
//
// A radial menu drawn on an authored surface: a round plate, a key for each
// entry at the slot the session lays it out at, the hot entry's variants on
// the outer ring, and a caption naming what a release would choose. Display
// only -- the session hit-tests the same layout -- and one screen for every
// wheel a workspace has, since only one is ever open.
//=============================================================================
class RadialMenuScreen
{
public:
    explicit RadialMenuScreen(UiService& ui);
    ~RadialMenuScreen();

    RadialMenuScreen(const RadialMenuScreen&) = delete;
    RadialMenuScreen& operator=(const RadialMenuScreen&) = delete;

    // The display-only surface over everything in the window; the screen
    // follows the workspace there. Invalid closes it.
    void MoveTo(UiSurfaceId overlay);
    void SetShown(bool shown);

    // Per frame: the open wheel, or none.
    void Present(const RadialMenuSession* wheel, const IRadialMenuModel* menu);

private:
    void Open();
    void Close();

    UiService& Ui;
    UiSurfaceId Surface;
    UiScreenHandle Screen;
    bool Shown = false;
};
