#pragma once

#include "IEditorPanel.h"

#include <core/console/ConsoleTypes.h>
#include <core/logging/LogLevel.h>

#include <array>
#include <cstddef>
#include <vector>

class ConsoleService;
class DebugLogSink;

class EditorConsolePanel : public IEditorPanel
{
public:
    EditorConsolePanel(DebugLogSink& sink, ConsoleService& console);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;
    void ToggleVisible() { Visible = !Visible; }
    void SetVisible(bool visible) { Visible = visible; }

private:
    DebugLogSink& Sink;
    ConsoleService& Console;
    bool Visible = false;
    std::array<bool, 5> LevelFilter = { true, true, true, true, true };
    char CategoryFilterBuf[128] = {};
    char CommandBuf[256] = {};
    std::vector<ConsoleOutputEntry> CommandOutput;
};
