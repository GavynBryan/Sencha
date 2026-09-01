// A scene load that fails partway must leave the destination registry as it
// found it: the loader creates entities before it knows whether every chunk
// resolves, so a rejected hierarchy edge or an unresolvable entity index has to
// unwind the entities and components already created.

#include <gtest/gtest.h>
#include <world/WorldComponentSchemas.h>

#include <core/json/JsonParser.h>
#include <core/serialization/Serialize.h>
#include <render/extract/Camera.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneFormat.h>
#include <world/serialization/SceneSerializer.h>

#include <sstream>

namespace
{

    Registry MakeFailureRegistry()
    {
        Registry registry;
        registry.Components.RegisterComponent<LocalTransform>();
        registry.Components.RegisterComponent<WorldTransform>();
        registry.Components.RegisterComponent<Parent>();
        registry.Components.RegisterComponent<CameraComponent>();
        return registry;
    }

    ComponentSerializerRegistry MakeSerializers()
    {
        ComponentSerializerRegistry serializers;
        RegisterEngineSceneSerializers(serializers);
        return serializers;
    }
}

TEST(SceneSerializerFailure, JsonLoadRollsBackEntitiesAndComponents)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "Transform": {
                        "local": {
                            "position": [1, 2, 3],
                            "rotation": [0, 0, 0, 1],
                            "scale": [1, 1, 1]
                        }
                    }
                }
            }
        ],
        "hierarchy": [
            { "child": 0, "parent": 4 }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded = MakeFailureRegistry();
    SceneLoadError error;
    EXPECT_FALSE(LoadSceneJson(*parsed, loaded, serializers, &error));

    EXPECT_EQ(loaded.Components.EntityCount(), 0u);
    EXPECT_EQ(loaded.Components.CountComponents<LocalTransform>(), 0u);
}

TEST(SceneSerializerFailure, HandlesEmptyRegistry)
{
    const ComponentSerializerRegistry serializers = MakeSerializers();
    Registry source;

    JsonValue json = SaveSceneJson(source, serializers);
    Registry jsonLoaded;
    ASSERT_TRUE(LoadSceneJson(json, jsonLoaded, serializers));
    EXPECT_EQ(jsonLoaded.Components.EntityCount(), 0u);
}
