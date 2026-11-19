#pragma once

#include <abilities/AbilityGrantEntries.h>
#include <effects/EffectId.h>

#include <cstdint>
#include <string>
#include <vector>

class World;
struct EquipmentSlot;
struct ItemDefinition;

[[nodiscard]] std::vector<AbilityGrantEntry> WornGrantEntries(const World& world, EffectId effect,
                                                              const EquipmentSlot& slot, const ItemDefinition& item);

void DeclareWornGrantEntries(World& world, std::vector<std::string>& errors);

[[nodiscard]] bool FindWornGrantEntries(const World& world, EffectId effect, const EquipmentSlot& slot,
                                        const ItemDefinition& item, std::vector<std::uint16_t>& out);
