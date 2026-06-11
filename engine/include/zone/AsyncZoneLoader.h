#pragma once

#include <jobs/AsyncTaskQueue.h>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

#include <functional>
#include <vector>

class Registry;
class RuntimeFrameLoop;
class ZoneRuntime;

//=============================================================================
// AsyncZoneLoader
//
// Loads zones without blocking the frame. BeginLoad submits an async task
// whose work stage builds a detached Registry (reserved id, invisible to the
// frame), and whose commit stage — at the per-frame drain point — attaches
// the registry to ZoneRuntime. The ZoneLoad temporal discontinuity is marked
// only when the zone attaches with any participation: a dormant attach (the
// default) is invisible to every frame span, so no discontinuity occurred.
//
// Room-streaming recipe (the engine's primary genre shape — preload the next
// room seamlessly while the current one plays, then flip it live at the door):
//
//   loader.BeginLoad(nextRoom, build, finalize);          // dormant: seamless
//   ... player crosses the doorway ...
//   zones.SetParticipation(nextRoom, { .Visible = true, .Logic = true });
//   runtime.MarkTemporalDiscontinuity(                    // only if the
//       TemporalDiscontinuityReason::Teleport);           // camera cuts
//
// Loading directly into an active state (initial spawn, fast travel) passes
// participation to BeginLoad and gets the ZoneLoad discontinuity with it.
//
// The build callback runs on a task thread against the detached registry
// only. It must not touch caches, services, or any other engine state;
// resolve asset handles on the main thread before BeginLoad and capture them
// by value (handles are plain values — the existing builder helpers already
// take them that way).
//
// All methods are owner-thread-only, inherited from AsyncTaskQueue's
// contract. The loader must outlive any load it has in flight.
//=============================================================================
class AsyncZoneLoader
{
public:
    using BuildFn = std::function<void(Registry&)>;
    using FinalizeFn = std::function<void(Registry&)>;

    AsyncZoneLoader(AsyncTaskQueue& tasks, ZoneRuntime& zones, RuntimeFrameLoop& runtime);

    // Starts loading. The zone must be valid, not loaded, and not in flight.
    AsyncTaskHandle BeginLoad(ZoneId zone, BuildFn build, ZoneParticipation participation = {});

    // As above, with a main-thread publish step: finalize runs inside the
    // commit, after the zone attaches and before the discontinuity is marked.
    // Unlike build, finalize runs on the owner thread and so may touch caches,
    // services, and game state — it is where cache acquisition, GPU uploads,
    // and entity wiring that needs ambient state belong. The frame never
    // observes the zone between attach and finalize; both happen inside one
    // drain callback.
    AsyncTaskHandle BeginLoad(ZoneId zone, BuildFn build, FinalizeFn finalize,
                              ZoneParticipation participation = {});

    // True from BeginLoad until the zone attaches (or its load is cancelled).
    [[nodiscard]] bool IsLoading(ZoneId zone) const;

    // Best effort, like AsyncTaskQueue::Cancel: fails (returns false) only
    // while the build is mid-flight on the task thread — retry next frame.
    bool CancelLoad(ZoneId zone);

private:
    struct InFlightLoad
    {
        ZoneId Zone;
        AsyncTaskHandle Handle;
    };

    void RemoveInFlight(ZoneId zone);

    AsyncTaskQueue& Tasks;
    ZoneRuntime& Zones;
    RuntimeFrameLoop& Runtime;
    std::vector<InFlightLoad> InFlight;
};
