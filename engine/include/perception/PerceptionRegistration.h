#pragma once

class ConsoleRegistry;
class EngineSchedule;

// Opt-in perception for a game: registers PerceptionSystem in FixedLogic. Call
// after RegisterPhysics, since sight needs the physics world to answer
// occlusion. `console` may be null; its tunables then keep their defaults.
void RegisterPerception(EngineSchedule& schedule, ConsoleRegistry* console);
