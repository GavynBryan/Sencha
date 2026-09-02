#pragma once

#include <core/identity/Id.h>
#include <ecs/ComponentAnnotations.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <world/serialization/SceneFieldCodec.h>

#include <string_view>

//=============================================================================
// PersistentIdComponent
//
// Carries an entity's persistent identity. The editor mints the id when the
// entity is authored; the cook carries it verbatim; the runtime resolves it
// through the world's PersistentEntityIndex. Editor documents must carry a
// valid id on every entity or they do not load; cooked scenes may hold
// entities with none (cook-generated content), which simply stay out of the
// index.
//
// Not removable: the document owns identity, not the inspector. Removing this
// would strand any save-overlay or cross-scene reference joined on the id, and
// the document would then fail to load. EditorScene is the only mutation
// boundary.
//=============================================================================
struct SENCHA_COMPONENT("persistent_id")
       SENCHA_SCHEMA("persistent_id")
       SENCHA_SCENE_CHUNK("PSID")
       SENCHA_NON_REMOVABLE
PersistentIdComponent
{
    SENCHA_FIELD("id")
    PersistentEntityId Id;
};

// Index membership: an identified entity resolves through the world's
// PersistentEntityIndex for exactly as long as it carries the component.
template <>
struct ComponentTraits<PersistentIdComponent>
{
    static void OnAdd(PersistentIdComponent& component, World& world, EntityId entity);
    static void OnRemove(const PersistentIdComponent& component, World& world, EntityId entity);
};

template <>
struct SceneFieldCodec<PersistentEntityId>
{
    static bool Save(IWriteArchive&, std::string_view, PersistentEntityId,
                     SceneSerializationContext&);
    static bool Load(IReadArchive&, std::string_view, PersistentEntityId&,
                     SceneSerializationContext&);
};

#if !defined(SENCHA_CODEGEN)
#  include <world/identity/PersistentIdComponent.sencha.h>
#endif
