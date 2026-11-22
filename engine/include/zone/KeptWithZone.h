#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <zone/ZoneStateLifetime.h>

#include <type_traits>

class World;

// A run-time entity its zone keeps in its `gained` list: docs/gameplay/saves.md, "What a zone gained".
struct SENCHA_COMPONENT("sencha.kept_with_zone") KeptWithZone
{
};

static_assert(std::is_empty_v<KeptWithZone>, "KeptWithZone is a tag: presence is its whole meaning");

// Authority only. Gives the entity a runtime persistent id if it has none; refuses a level entity and a held record.
bool KeepWithZone(World& world, EntityId entity);

[[nodiscard]] bool IsKeptWithZone(const World& world, EntityId entity);

// Its own ZoneStateLifetime, else never.
[[nodiscard]] ZoneStateForgetting ForgettingOfKept(const World& world, EntityId entity);

#if !defined(SENCHA_CODEGEN)
#  include <zone/KeptWithZone.sencha.h>
#endif
