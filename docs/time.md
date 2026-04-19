# TimeService

`TimeService` is the source of truth for all engine timing. It owns a `steady_clock` and produces a `FrameTime` snapshot each frame via `Advance()`. Call `Advance()` exactly once per frame before dispatching frame systems through `EngineSchedule`.

---

## Location

```
engine/include/time/TimeService.h
```

```cpp
#include <time/TimeService.h>
```

---

## Timing Model

`TimeService` tracks elapsed time and frame delta using a monotonic clock. Each frame, `Advance()` computes the time since the last frame, clamps the delta to avoid simulation spikes, and updates both scaled and unscaled time.

- **UnscaledDeltaTime** is clamped to a maximum of ~1/15s to absorb stalls (e.g., alt-tab, debugger break, first frame).
- **Timescale** multiplies DeltaTime and ElapsedTime, but does not affect their unscaled counterparts. Setting timescale to 0 pauses simulation time while unscaled time continues (useful for pause menus or freeze-frames).

---

## API

```cpp
class TimeService : public IService
{
public:
    TimeService();

    // Advance the clock by one frame. Returns the FrameTime snapshot for
    // this frame. Must be called exactly once per frame, before any systems run.
    FrameTime Advance();

    void SetTimescale(float scale);
    float GetTimescale() const;

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr float MaxDeltaSeconds = 1.0f / 15.0f;
    TimePoint LastTime;
    float Timescale = 1.0f;
    float ElapsedTime = 0.0f;
    float UnscaledElapsedTime = 0.0f;
    bool FirstFrame = true;
};
```

---

## Usage

### Per-frame timing

Call `Advance()` once per frame, before any systems run. Pass the returned `FrameTime` snapshot to systems that require timing information.

```cpp
FrameTime frameTime = timeService.Advance();
// ... pass frameTime to systems ...
```

### Timescale control

Pause or slow down simulation by adjusting the timescale:

```cpp
timeService.SetTimescale(0.0f); // Pause simulation
timeService.SetTimescale(0.5f); // Half-speed simulation
float scale = timeService.GetTimescale();
```

---

## Delta Clamping

To prevent simulation spikes, `Advance()` clamps the unscaled frame delta to `MaxDeltaSeconds` (~1/15s). This ensures that a single long stall (e.g., alt-tab, first frame) does not produce a large physics or gameplay step.

---

## Constraints

**Do not add game logic or system state to TimeService.** It must remain a pure timing provider. Pause state, gameplay timers, and other logic belong in higher-level systems that consume `FrameTime`.
