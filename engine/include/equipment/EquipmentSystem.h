#pragma once

#include <time/FrameClock.h>

#include <cstdint>

class World;
struct FixedLogicContext;

void RunEquipmentPass(World& world, const FixedSimTime& now);
[[nodiscard]] std::uint64_t EquipmentRebindWalks(const World& world);

class EquipmentSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
};

// False, leaving the World's admission as it is, when another layer installed one.
[[nodiscard]] bool InstallEquipmentMoveAdmission(World& world);
[[nodiscard]] bool HoldsEquipmentMoveAdmission(const World& world);
