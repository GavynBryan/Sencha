#pragma once

#include <graphics/RenderExtent.h>

class EngineSchedule;
class RuntimeFrameLoop;
class StoragePartitionSet;
class World;
struct EngineConfig;
struct PresentedFrame;

// The frame's presentation step: publishes the view this frame is drawn with
// (camera/PresentedFrame.h) for the runtime's current frame, then runs every
// PresentationUpdate system against it. The frame loop calls it once a frame,
// after RunFrameUpdate and before UI layout, and extraction then reuses what it
// published. `extent` is what the frame draws into; empty on a host that draws
// nothing, which publishes an invalid frame and presents no camera effects.
const PresentedFrame& RunPresentationPhase(EngineSchedule& schedule,
                                           EngineConfig& config,
                                           RuntimeFrameLoop& runtime,
                                           World& world,
                                           const StoragePartitionSet& visible,
                                           RenderExtent extent);
