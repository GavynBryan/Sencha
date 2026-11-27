#pragma once

#include <core/json/JsonValue.h>
#include <core/metadata/ComponentPersistence.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <world/serialization/SceneSerializationContext.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

class ComponentSerializerRegistry;
struct IComponentSerializer;
struct PackageEntity;
class World;

// An entity's components as one record, and the patch between two: docs/gameplay/saves.md, "The entity patch".

struct ComponentPersistenceSet
{
    bool Authored = false;
    bool Saved = false;
    bool Identity = false;

    [[nodiscard]] bool Contains(ComponentPersistence persistence) const
    {
        switch (persistence)
        {
        case ComponentPersistence::Authored:
            return Authored;
        case ComponentPersistence::Saved:
            return Saved;
        case ComponentPersistence::Identity:
            return Identity;
        }
        return false;
    }
};

struct AuthoredComponent
{
    ComponentTypeId Type;
    // Null when content holds the component as runtime bytes or, with InContent false, not at all.
    JsonValue Record;
    bool InContent = true;
    // False: listed, so not taken as gained, and left out of the patch.
    bool Diffed = true;
};

struct EntityScope
{
    std::span<const ComponentTypeId> Listed = {};
    ComponentPersistenceSet Declared = {};
};

// What a Saved component content did not author is diffed against; nullopt when it has none.
[[nodiscard]] std::optional<JsonValue> SavedDefaultRecord(const IComponentSerializer& serializer);

[[nodiscard]] std::vector<AuthoredComponent> AuthoredComponentsOf(const PackageEntity& entity,
                                                                 const ComponentSerializerRegistry& serializers,
                                                                 const EntityScope& scope);

[[nodiscard]] JsonValue WriteEntityRecord(const World& world, EntityId entity,
                                          const ComponentSerializerRegistry& serializers,
                                          SceneSerializationContext& context, const EntityScope& scope,
                                          SceneRecordPurpose purpose, std::vector<std::string>* failed = nullptr);

// False at the first member that does not read whole, named in `failed`; what was read stays on the entity.
[[nodiscard]] bool ReadEntityRecord(World& world, EntityId entity, const JsonValue& record,
                                    const ComponentSerializerRegistry& serializers, SceneSerializationContext& context,
                                    std::string* failed = nullptr);

// `unchanged` names components whose saved form is what `kept` was taken from: their members are kept, unwritten.
[[nodiscard]] JsonValue CaptureEntityPatch(const World& world, EntityId entity,
                                           std::span<const AuthoredComponent> baseline,
                                           ComponentPersistenceSet alsoCarried,
                                           const ComponentSerializerRegistry& serializers,
                                           SceneSerializationContext& context, const JsonValue* kept = nullptr,
                                           std::vector<std::string>* failed = nullptr,
                                           std::span<const ComponentTypeId> unchanged = {});

struct EntityPatchOutcome
{
    std::vector<std::string> Retuned;
    std::vector<std::string> Misfit;
    // The patch's members, by component key, that Misfit's lines left out.
    std::vector<std::string> MisfitMembers;
    std::vector<std::string> Unknown;
};

[[nodiscard]] EntityPatchOutcome ApplyEntityPatch(PackageEntity& entity, const JsonValue& patch,
                                                  const ComponentSerializerRegistry& serializers);
