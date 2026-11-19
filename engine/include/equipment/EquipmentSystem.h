#pragma once

#include <time/FrameClock.h>

#include <cstdint>

class EngineSchedule;
class World;
struct FixedLogicContext;

void RunEquipmentPass(World& world, const FixedSimTime& now);
[[nodiscard]] std::uint64_t EquipmentRebindWalks(const World& world);

class EquipmentSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};

void RegisterEquipmentSystems(EngineSchedule& schedule);

void InstallEquipmentMoveAdmission(World& world);
