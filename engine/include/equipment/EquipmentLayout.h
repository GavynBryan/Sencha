#pragma once

#include <abilities/AbilityDeclaration.h>
#include <core/identity/NamedDefinitionRegistry.h>
#include <core/identity/StrongId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class World;

using EquipmentLayoutId = StrongId<struct EquipmentLayoutIdTag, std::uint32_t>;

enum class SlotDepletion : std::uint8_t
{
    Release,
    Hold,
};

struct EquipmentSlot
{
    std::string Key;
    GameplayTagId Accepts;
    AbilityBindingDecl Binding;
    std::int32_t Order = 0;
    std::string Socket;
    SlotDepletion OnDepleted = SlotDepletion::Release;
};

struct EquipmentLayout
{
    std::vector<EquipmentSlot> Slots;
    std::vector<GameplayTagId> LockTags;
};

inline constexpr std::size_t kMaxEquipmentSlots = 32;
inline constexpr std::uint8_t kNoEquipmentSlot = 0xFF;

[[nodiscard]] const EquipmentLayout* FindEquipmentLayout(const World& world, EquipmentLayoutId id);
[[nodiscard]] std::uint8_t FindEquipmentSlot(const EquipmentLayout& layout, std::string_view key);

[[nodiscard]] std::vector<std::uint8_t> EquipmentSlotsNamed(const EquipmentLayout& layout, std::string_view name);

class EquipmentLayoutRegistry final : public NamedDefinitionRegistry<EquipmentLayoutId, EquipmentLayout>
{
};
