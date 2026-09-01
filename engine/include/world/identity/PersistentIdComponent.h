#pragma once

#include <core/identity/Id.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <string_view>
#include <tuple>

//=============================================================================
// PersistentIdComponent
//
// Carries an entity's persistent identity. The editor mints the id when the
// entity is authored; the cook carries it verbatim; the runtime resolves it
// through the world's PersistentEntityIndex. Editor documents must carry a
// valid id on every entity or they do not load; cooked scenes may hold
// entities with none (cook-generated content), which simply stay out of the
// index.
//=============================================================================
struct PersistentIdComponent
{
    PersistentEntityId Id;
};

SENCHA_DECLARE_COMPONENT_TYPE(PersistentIdComponent, "persistent_id");
SENCHA_COMPONENT_DECLARES_SCHEMA(PersistentIdComponent);
SENCHA_COMPONENT_DECLARES_TRAITS(PersistentIdComponent);
