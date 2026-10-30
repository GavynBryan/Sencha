#include <app/PresentationPhase.h>

#include <app/EngineSchedule.h>
#include <camera/PresentedFrame.h>

const PresentedFrame& RunPresentationPhase(EngineSchedule& schedule,
                                           EngineConfig& config,
                                           RuntimeFrameLoop& runtime,
                                           World& world,
                                           const StoragePartitionSet& visible,
                                           RenderExtent extent)
{
    const RuntimeFrameSnapshot& frame = runtime.GetCurrentFrame();
    const PresentedFrame& presented =
        PublishPresentedFrame(world, runtime.GetSimulationClock(), frame.Presentation, extent, visible);
    PresentationUpdateContext ctx{
        .Config = config,
        .Runtime = runtime,
        .WallDeltaSeconds = static_cast<double>(frame.WallTime.Dt),
        .Presentation = frame.Presentation,
        .Frame = presented,
        .Entities = world,
        .Partitions = visible,
    };
    schedule.RunPresentationUpdate(ctx);
    return presented;
}
