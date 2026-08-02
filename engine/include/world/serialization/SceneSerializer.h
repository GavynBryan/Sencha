#pragma once

#include <core/json/JsonValue.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

//=============================================================================
// SceneSaveError / SceneLoadError
//
// Populated on failure; Message contains a human-readable description of the
// error. Pass a pointer to either type into Save/Load functions to receive it.
//=============================================================================
struct SceneSaveError
{
    std::string Message;
};

struct SceneLoadError
{
    std::string Message;
};

// Which components a scene can carry is a property of the host, not of the
// process: the runtime's set is the engine manifest plus whatever the loaded
// game module registers, an editor adds its authoring-only components, and a
// test wants only what it declared. Each host owns a ComponentSerializerRegistry
// and passes it in, so those sets cannot leak into each other and a test cannot
// be perturbed by what ran before it.

template <typename Component>
void RegisterComponent(ComponentSerializerRegistry& serializers)
{
    // Fail loud on a genuine collision: a component whose type id, chunk id, or
    // JSON key is already taken would otherwise serialize as another component.
    const auto result =
        serializers.Register(std::make_unique<ComponentSerializer<Component>>());
    assert(result != ComponentSerializerRegistry::RegisterResult::Rejected
           && "Component serializer collision: ComponentTypeId, chunk ID, or JSON "
              "key already registered by a different component");
    (void)result;
}

// Registers every component in the engine's scene manifest.
void RegisterEngineSceneSerializers(ComponentSerializerRegistry& serializers);

[[nodiscard]] bool SaveSceneBinary(const Registry& registry,
    const ComponentSerializerRegistry& serializers,
    BinaryWriter& writer,
    SceneSaveError* error = nullptr);
[[nodiscard]] bool SaveSceneBinary(const Registry& registry,
    const ComponentSerializerRegistry& serializers,
    BinaryWriter& writer,
    SceneSerializationContext& context,
    SceneSaveError* error = nullptr);
[[nodiscard]] bool LoadSceneBinary(BinaryReader& reader,
    Registry& registry,
    const ComponentSerializerRegistry& serializers,
    SceneLoadError* error = nullptr);
[[nodiscard]] bool LoadSceneBinary(BinaryReader& reader,
    Registry& registry,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& context,
    SceneLoadError* error = nullptr);

[[nodiscard]] JsonValue SaveSceneJson(const Registry& registry,
    const ComponentSerializerRegistry& serializers);
[[nodiscard]] JsonValue SaveSceneJson(const Registry& registry,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& context);
[[nodiscard]] bool LoadSceneJson(const JsonValue& root,
    Registry& registry,
    const ComponentSerializerRegistry& serializers,
    SceneLoadError* error = nullptr);
[[nodiscard]] bool LoadSceneJson(const JsonValue& root,
    Registry& registry,
    const ComponentSerializerRegistry& serializers,
    SceneSerializationContext& context,
    SceneLoadError* error = nullptr);
