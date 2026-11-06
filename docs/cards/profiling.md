# profiling

Snapshot `97ed48e4` (2026-10-30). Layer: foundation.

## 1. Frame phases

- `ExtractRender`: a `CpuScopeTimer` times UI extraction at `engine/src/app/EngineFramePhases.cpp:1167`, and others wrap each render extraction step, starting with `CpuScopeTimer` at `engine/src/app/DefaultRenderPipeline.cpp:243`.
- `Render`: GPU scopes, stats and captures are collected by the renderer during the phase registered at `engine/src/app/EngineFramePhases.cpp:1177`.

## 2. Inputs

- The instrumentation tier from text, through `ParseRenderProfileMode` (`engine/include/profiling/RenderInstrumentation.h:27`).
- Scope timings from `CpuScopeTimer` (`engine/include/profiling/CpuScopeTimings.h:85`) and counters written by the renderer.
- Module dependencies: core, graphics, time.

## 3. Outputs

- `CpuScopeTimings` (`engine/include/profiling/CpuScopeTimings.h:54`) and `RenderStats` (`engine/include/profiling/RenderStats.h:16`): read by app, graphics and render; `RenderStats` also by debug.
- `RenderStatsHistory` (`engine/include/profiling/RenderStats.h:103`) and `RenderCapture` (`engine/include/profiling/RenderCapture.h:23`): written to CSV by a console command in `engine/src/app/EngineConsoleBuiltins.cpp`.

## 4. Central decisions

- `ParseRenderProfileMode` (`engine/include/profiling/RenderInstrumentation.h:27`): picks the instrumentation tier, each tier including everything below it.
- `RenderCapture::SerializeCsv` (`engine/src/profiling/RenderCapture.cpp:228`): decides the capture file's columns and rows.

## 5. Invariants

- The off tier hands out only null sinks, so it cannot write: `RenderInstrumentationBundle.OffTierYieldsAllNullSoTheOffPathCannotWrite` (`test/core/RenderInstrumentationBundleTests.cpp`).
- The counters tier enables stats and history only: `RenderInstrumentationBundle.CountersTierEnablesStatsAndHistoryOnly` (`test/core/RenderInstrumentationBundleTests.cpp`).
- Unmeasured scopes stay negative and measured ones accumulate: `CpuScopeTimings.UnmeasuredScopesStayNegativeAndMeasuredOnesAccumulate` (`test/core/RenderProfilingTests.cpp`).
- A timer with no sink records nothing: `CpuScopeTimings.ATimerWithNoSinkRecordsNothing` (`test/core/RenderProfilingTests.cpp`).

### Unchecked

- None identified in this pass.

## 6. Size

- 788 lines in 7 files.
- CCN 10 `RenderCapture::SerializeCsv` (`engine/src/profiling/RenderCapture.cpp:228`); CCN 10 `ToString` (`engine/src/profiling/CpuScopeTimings.cpp:3`); CCN 9 `ToString` (`engine/src/profiling/RenderInstrumentation.cpp:15`).

## 7. Docs that contradict the code

- None found.
