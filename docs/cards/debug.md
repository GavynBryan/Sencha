# debug

Snapshot `97ed48e4` (2026-10-30). Layer: presentation.

## 1. Frame phases

- `PumpPlatform`: the overlay takes primary-window events through `ProcessSdlEvent` (`engine/src/app/EngineFramePhases.cpp:978`).
- `Render`: `ImGuiDebugOverlay` (`engine/include/debug/ImGuiDebugOverlay.h:22`) is an `IRenderFeature` and draws its panels when the renderer records.
- `ExtractRender`: debug lines are copied by `ExtractDebugLines` (`engine/include/render/extract/DebugLineExtraction.h:23`).

## 2. Inputs

- Panels added by app (`engine/src/app/Engine.cpp`), each an `IDebugPanel` (`engine/include/debug/IDebugPanel.h:16`), reading net, navigation, perception, movement, render and timing state.
- Lines drawn by gameplay into `DebugLineChannels` (`engine/include/debug/DebugLines.h:42`).
- Module dependencies: assets, attributes, core, ecs, gameplay_tags, graphics, math, movement, navigation, net, perception, platform, profiling, render, time, world.

## 3. Outputs

- The ImGui overlay and console, `DrawConsoleView` (`engine/include/debug/ConsoleView.h:50`).
- Debug line geometry, `DebugLineList` (`engine/include/debug/DebugLines.h:21`): written by navigation and perception, copied out by render extraction.

## 4. Central decisions

- `DrawConsoleView` (`engine/src/debug/ConsoleView.cpp:132`): draws the console log, history and completion.
- `NetStatsPanel::Draw` (`engine/src/debug/NetStatsPanel.cpp:79`): lays out traffic and session state.
- `NavAgentPanel::Draw` (`engine/src/debug/NavAgentPanel.cpp:40`): lays out agent orders, routes and failures.

## 5. Invariants

- A channel is found by name and keeps its lines until cleared: `DebugLines.AChannelIsFoundByNameAndKeepsItsLinesUntilCleared` (`test/runtime/DebugLineExtractionTests.cpp`).
- Extraction copies every channel into vertex pairs by depth: `DebugLines.ExtractionCopiesEveryChannelIntoVertexPairsByDepth` (`test/runtime/DebugLineExtractionTests.cpp`).
- A narrow cone stays inside its angle and range: `DebugLines.ANarrowConeStaysInsideItsAngleAndRange` (`test/runtime/DebugLineExtractionTests.cpp`).
- Nothing is drawn until asked: `PerceptionDebugDraw.NothingIsDrawnUntilAsked` (`test/spatial/perception/PerceptionDebugDrawTests.cpp`).

### Unchecked

- The panels themselves: no test opens `NetStatsPanel`, `NavAgentPanel` or the console view.

## 6. Size

- 2,678 lines in 28 files.
- CCN 35 `DrawConsoleView` (`engine/src/debug/ConsoleView.cpp:132`); CCN 26 `NetStatsPanel::Draw` (`engine/src/debug/NetStatsPanel.cpp:79`); CCN 24 `NavAgentPanel::Draw` (`engine/src/debug/NavAgentPanel.cpp:40`).

## 7. Docs that contradict the code

- None found.
