#pragma once

#include "IEditorPanel.h"

#include "../tools/ToolRegistry.h"
#include "../viewport/FourWayViewportLayout.h"

union SDL_Event;

class ViewportPanel : public IEditorPanel
{
public:
    ViewportPanel(FourWayViewportLayout& layout, ToolRegistry& tools);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;

    bool ProcessSdlEvent(const SDL_Event& event);

private:
    void DrawViewport(EditorViewport& viewport,
                      const char* id,
                      const char* label,
                      int index,
                      ImVec2 size);
    void ClearViewportCapture();
    bool UpdateActiveViewport(ImVec2 point, bool captureFlyCamera, bool captureOrthoPan);

    FourWayViewportLayout& Layout;
    ToolRegistry& Tools;
    bool Visible = true;
};
