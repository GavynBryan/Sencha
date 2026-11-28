#pragma once

#include "properties/EntityReferenceProperty.h"

#include <core/identity/Id.h>
#include <ecs/EntityId.h>
#include <zone/ZoneId.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class CommandStack;
class EditorDocument;
class SelectionService;
class World;
class WorldDocument;

struct EntityReferenceSubject
{
    const EditorDocument& Document;
    const World& Components;
    EntityId Entity;
};

// Which entities a reference field may name, supplied by the field.
struct EntityReferenceFilter
{
    // Null for every loaded document of the world, the world scene among them.
    const EditorDocument* Within = nullptr;
    // Empty admits every entity with a persistent identity.
    std::function<bool(const EntityReferenceSubject&)> Admits;
};

[[nodiscard]] std::vector<EntityReferenceChoice> EntityReferenceCandidates(WorldDocument& world,
                                                                           const EntityReferenceFilter& filter);
[[nodiscard]] std::optional<EntityReferenceSubject> AdmittedEntityReference(WorldDocument& world,
                                                                            const EntityReferenceFilter& filter,
                                                                            PersistentEntityId target);

enum class EntityReferenceState : std::uint8_t
{
    None,
    Loaded,
    NotLoaded, // expected in a zone that is not loaded
    Unlocated, // in no loaded document while some zone is not loaded
    Missing,   // in no document, every one of them loaded
    Malformed,
};

struct EntityReferenceReading
{
    EntityReferenceState State = EntityReferenceState::None;
    PersistentEntityId Target;
    const EditorDocument* Document = nullptr;
    EntityId Entity;
    bool InWorldScene = false;
    ZoneId Zone;
    std::string Text;
};

// `expected` is the zone the holder records for its target, if it records one.
[[nodiscard]] EntityReferenceReading ReadEntityReference(WorldDocument& world, std::string_view value,
                                                         ZoneId expected = {});

void ShowEntityReference(PropertyNode& node, const EntityReferenceReading& reading);
// Every entity reference in the tree that does not yet say how it reads.
void ShowEntityReferences(std::vector<PropertyNode>& nodes, WorldDocument& world);

// A target in another loaded document is focused first, which clears the undo
// history as every focus change does. False when the target is not loaded.
bool SelectEntityReference(WorldDocument& world, SelectionService& selection, CommandStack& commands,
                           std::string_view value);
