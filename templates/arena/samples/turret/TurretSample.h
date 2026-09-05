#pragma once

class ArenaSessionPolicy;
class Engine;
class EngineSchedule;

// The turret sample's three touch points in the game, so removing the sample is
// removing these calls. Install binds the request payload and registers the
// `turret` console command; RegisterSystems adds the systems that aim a turret
// and settle a placed one.
void InstallTurretSample(Engine& engine, ArenaSessionPolicy& session);
void RegisterTurretSampleSystems(Engine& engine, EngineSchedule& schedule);
