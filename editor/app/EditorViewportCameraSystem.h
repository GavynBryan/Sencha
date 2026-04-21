#pragma once

#include <app/GameContexts.h>

class FourWayViewportLayout;

class EditorViewportCameraSystem
{
public:
    explicit EditorViewportCameraSystem(FourWayViewportLayout& layout);

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    FourWayViewportLayout& Layout;
};
