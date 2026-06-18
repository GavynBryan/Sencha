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
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Bottom; }

private:
    DebugLogSink& Sink;
    ConsoleService& Console;
    std::array<bool, 5> LevelFilter = { true, true, true, true, true };
    char CategoryFilterBuf[128] = {};
    char CommandBuf[256] = {};
    std::vector<ConsoleOutputEntry> CommandOutput;
};
