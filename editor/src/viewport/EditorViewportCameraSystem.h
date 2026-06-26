#pragma once

#include <app/GameContexts.h>

class ViewportLayout;

class EditorViewportCameraSystem
{
public:
    explicit EditorViewportCameraSystem(ViewportLayout& layout);

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    ViewportLayout& Layout;
};
