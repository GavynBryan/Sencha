#pragma once

#include <graphics/RenderExtent.h>

class EngineSchedule;
class RuntimeFrameLoop;
class StoragePartitionSet;
class World;
struct EngineConfig;
struct PresentedFrame;

const PresentedFrame& RunPresentationPhase(EngineSchedule& schedule,
                                           EngineConfig& config,
                                           RuntimeFrameLoop& runtime,
                                           World& world,
                                           const StoragePartitionSet& visible,
                                           RenderExtent extent);
