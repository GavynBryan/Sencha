#pragma once

#include "IViewportLayout.h"

#include "EditorViewport.h"

class FourWayViewportLayout : public IViewportLayout
{
public:
    FourWayViewportLayout();

    EditorViewport Viewports[4];
    int ActiveIndex = 0;

    std::span<EditorViewport> GetViewports() override;
    EditorViewport* GetActiveViewport() override;
    void OnResize(uint32_t width, uint32_t height) override;

private:
    uint32_t Width = 0;
    uint32_t Height = 0;
};
