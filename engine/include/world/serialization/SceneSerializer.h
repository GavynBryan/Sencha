#pragma once

#include <core/json/JsonValue.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>

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

void RegisterComponentSerializer(std::unique_ptr<IComponentSerializer> serializer);
void ClearComponentSerializers();
void InitSceneSerializer();

template <typename Component>
void RegisterComponent()
{
    RegisterComponentSerializer(std::make_unique<ComponentSerializer<Component>>());
}

[[nodiscard]] const std::vector<std::unique_ptr<IComponentSerializer>>& GetComponentSerializerEntries();

[[nodiscard]] bool SaveSceneBinary(const Registry& registry, BinaryWriter& writer,
    SceneSaveError* error = nullptr);
[[nodiscard]] bool SaveSceneBinary(const Registry& registry,
    BinaryWriter& writer,
    SceneSerializationContext& context,
    SceneSaveError* error = nullptr);
[[nodiscard]] bool LoadSceneBinary(BinaryReader& reader, Registry& registry,
    SceneLoadError* error = nullptr);
[[nodiscard]] bool LoadSceneBinary(BinaryReader& reader,
    Registry& registry,
    SceneSerializationContext& context,
    SceneLoadError* error = nullptr);

[[nodiscard]] JsonValue SaveSceneJson(const Registry& registry);
[[nodiscard]] JsonValue SaveSceneJson(const Registry& registry,
    SceneSerializationContext& context);
[[nodiscard]] bool LoadSceneJson(const JsonValue& root, Registry& registry,
    SceneLoadError* error = nullptr);
[[nodiscard]] bool LoadSceneJson(const JsonValue& root,
    Registry& registry,
    SceneSerializationContext& context,
    SceneLoadError* error = nullptr);
