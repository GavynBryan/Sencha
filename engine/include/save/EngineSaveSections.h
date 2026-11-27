#pragma once

#include <ecs/EntityId.h>

#include <string>
#include <vector>

class ComponentSerializerRegistry;
class Engine;
class SavedGames;
class World;

// The sections every save carries beside the id counter SavedGames writes: the
// local participant's body ("sencha.bodies") and what the world and that body
// hold ("sencha.held").
void RegisterEngineSaveSections(SavedGames& saves, Engine& engine);

// What a save holds of one entity: a line saying how, then one per component (save.describe).
[[nodiscard]] std::vector<std::string> DescribeSavedEntity(const World& world, EntityId entity,
                                                           const ComponentSerializerRegistry& serializers);
