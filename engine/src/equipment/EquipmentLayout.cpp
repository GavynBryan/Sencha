#include <equipment/EquipmentLayout.h>

#include <ecs/World.h>

const EquipmentLayout* FindEquipmentLayout(const World& world, EquipmentLayoutId id)
{
    const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
    return layouts != nullptr ? layouts->Get(id) : nullptr;
}

std::uint8_t FindEquipmentSlot(const EquipmentLayout& layout, std::string_view key)
{
    for (std::size_t i = 0; i < layout.Slots.size(); ++i)
        if (layout.Slots[i].Key == key)
            return static_cast<std::uint8_t>(i);
    return kNoEquipmentSlot;
}

std::vector<std::uint8_t> EquipmentSlotsNamed(const EquipmentLayout& layout, std::string_view name)
{
    if (const std::uint8_t exact = FindEquipmentSlot(layout, name); exact != kNoEquipmentSlot)
        return { exact };
    std::vector<std::uint8_t> group;
    for (std::size_t i = 0; i < layout.Slots.size(); ++i)
    {
        const std::string_view key = layout.Slots[i].Key;
        if (!name.empty() && key.size() > name.size() && key.starts_with(name) && key[name.size()] == '.')
            group.push_back(static_cast<std::uint8_t>(i));
    }
    return group;
}
