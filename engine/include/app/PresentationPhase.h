#pragma once

class EngineSchedule;
class RuntimeFrameLoop;
class StoragePartitionSet;
class World;
struct EngineConfig;
struct PresentedFrame;
struct RenderExtent;

const PresentedFrame& RunPresentationPhase(EngineSchedule& schedule,
                                           EngineConfig& config,
                                           RuntimeFrameLoop& runtime,
                                           World& world,
                                           const StoragePartitionSet& visible,
                                           RenderExtent extent);
