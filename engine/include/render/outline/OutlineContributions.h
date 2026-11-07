#pragma once

#include <ecs/EntityId.h>
#include <presentation/PresentationOwner.h>
#include <render/outline/OutlineStyles.h>

#include <cstdint>
#include <string_view>

class World;

// Null for a sixteenth style or a priority already taken. Styles are per World, so
// a game owns its own and the editor owns its own.
[[nodiscard]] OutlineStyleId RegisterOutlineStyle(World& world, std::string_view name, const OutlineStyleDesc& desc);

struct OutlineContribution
{
    PresentationOwner Owner;
    OutlineStyleId Style;
    // The entity's one style is the highest Order, then the owner that precedes.
    std::int16_t Order = 0;
};

void SetOutline(World& world, EntityId entity, const OutlineContribution& contribution);
void ClearOutline(World& world, EntityId entity, const PresentationOwner& owner);
void ClearOutlinesOf(World& world, PresentationWriterId writer);
[[nodiscard]] bool IsOutlined(const World& world, EntityId entity);

