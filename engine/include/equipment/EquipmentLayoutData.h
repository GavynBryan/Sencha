#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <core/metadata/DataSchema.h>
#include <equipment/EquipmentLayout.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class World;

inline constexpr std::string_view kEquipmentLayoutType = "equipment.layout";

struct EquipmentLayoutDeclaration
{
    struct Slot
    {
        std::string Key;
        std::string Accepts;
        std::optional<std::string> Action;
        std::optional<std::int32_t> Priority;
        std::int32_t Order = 0;
        std::string Socket;
        SlotDepletion OnDepleted = SlotDepletion::Release;
    };

    std::vector<Slot> Slots;
    std::vector<std::string> LockTags;
};

void RegisterEquipmentLayoutData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

[[nodiscard]] bool ResolveEquipmentLayoutDeclaration(const EquipmentLayoutDeclaration& declaration, const World& world,
                                                     EquipmentLayout& out, std::vector<std::string>& errors);
