#include <gtest/gtest.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/Field.h>
#include <core/serialization/BinaryArchive.h>
#include <core/serialization/BinaryReader.h>
#include <core/serialization/BinaryWriter.h>
#include <core/serialization/FourCC.h>
#include <render/IrradianceVolumeComponent.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>
#include <tuple>

namespace
{
struct SceneSignedOffset
{
    std::int32_t Offset = 0;
    std::int16_t Small = 0;
};

struct SceneNestedSignedComponent
{
    SceneSignedOffset Inner;
};
}

template <>
struct TypeSchema<SceneSignedOffset>
{
    static constexpr std::string_view Name = "SceneSignedOffset";

    static auto Fields()
    {
        return std::tuple{
            MakeField("offset", &SceneSignedOffset::Offset),
            MakeField("small", &SceneSignedOffset::Small),
        };
    }
};

template <>
struct TypeSchema<SceneNestedSignedComponent>
{
    static constexpr std::string_view Name = "SceneNestedSigned";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('T', 'N', 'S', 'I');

    static auto Fields()
    {
        return std::tuple{
            MakeField("inner", &SceneNestedSignedComponent::Inner),
        };
    }
};

namespace
{
Registry MakeEngineRegistry()
{
    Registry registry;
    ComponentRegistrar components(registry.Components);
    RegisterEngineComponents(components);
    return registry;
}

ComponentSerializerRegistry MakeEngineSerializers()
{
    ComponentSerializerRegistry serializers;
    RegisterEngineSceneSerializers(serializers);
    return serializers;
}

const JsonValue* ComponentOfFirstEntity(const JsonValue& scene, std::string_view key)
{
    const JsonValue* entities = scene.Find("entities");
    if (entities == nullptr || !entities->IsArray() || entities->AsArray().empty())
        return nullptr;
    const JsonValue* components = entities->AsArray()[0].Find("components");
    return components != nullptr ? components->Find(key) : nullptr;
}

template <typename Component>
const Component* OnlyComponent(const Registry& registry)
{
    const Component* found = nullptr;
    registry.Components.ForEachComponent<Component>([&](EntityId, const Component& component) { found = &component; });
    return found;
}

bool RoundTripsThroughBinary(const ComponentSerializerRegistry& serializers, std::string_view key,
                             Registry& source, EntityId entity, Registry& loaded, EntityId into)
{
    IComponentSerializer* serializer = serializers.FindByJsonKey(key);
    EXPECT_NE(serializer, nullptr);
    if (serializer == nullptr)
        return false;
    LoggingProvider logging;
    std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
    {
        BinaryWriter writer(bytes);
        BinaryWriteArchive out(writer);
        SceneSerializationContext context{ logging };
        if (!serializer->Save(out, entity, source.Components, context))
            return false;
    }
    bytes.seekg(0);
    BinaryReader reader(bytes);
    BinaryReadArchive in(reader);
    SceneSerializationContext context{ logging };
    return serializer->Load(in, into, loaded, context);
}
}

TEST(SceneIntegerField, ANegativeComponentMemberRoundTripsThroughJsonAndBinary)
{
    const ComponentSerializerRegistry serializers = MakeEngineSerializers();
    Registry source = MakeEngineRegistry();
    const EntityId entity = source.Components.CreateEntity();
    source.Components.AddComponent(entity, IrradianceVolumeComponent{ .Priority = -1 });

    const JsonValue scene = SaveSceneJson(source, serializers);
    const JsonValue* volume = ComponentOfFirstEntity(scene, "IrradianceVolume");
    ASSERT_NE(volume, nullptr);
    ASSERT_NE(volume->Find("priority"), nullptr);
    EXPECT_EQ(volume->Find("priority")->AsNumber(), -1.0) << "written as the number, not 4294967295";

    const std::optional<JsonValue> parsed = JsonParse(JsonStringify(scene, true));
    ASSERT_TRUE(parsed.has_value());
    Registry loaded = MakeEngineRegistry();
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers, &error)) << error.Message;
    ASSERT_NE(OnlyComponent<IrradianceVolumeComponent>(loaded), nullptr);
    EXPECT_EQ(OnlyComponent<IrradianceVolumeComponent>(loaded)->Priority, -1);

    Registry binary = MakeEngineRegistry();
    const EntityId into = binary.Components.CreateEntity();
    ASSERT_TRUE(RoundTripsThroughBinary(serializers, "IrradianceVolume", source, entity,
                                                                   binary, into));
    ASSERT_NE(OnlyComponent<IrradianceVolumeComponent>(binary), nullptr);
    EXPECT_EQ(OnlyComponent<IrradianceVolumeComponent>(binary)->Priority, -1);
}

TEST(SceneIntegerField, AnAuthoredVolumeWithANegativePriorityLoads)
{
    const ComponentSerializerRegistry serializers = MakeEngineSerializers();
    const std::optional<JsonValue> parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            { "components": { "IrradianceVolume": { "half_extents": [4, 2, 4], "cell_size": 1.0, "priority": -1 } } }
        ]
    })");
    ASSERT_TRUE(parsed.has_value());

    Registry loaded = MakeEngineRegistry();
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers, &error)) << error.Message;
    ASSERT_NE(OnlyComponent<IrradianceVolumeComponent>(loaded), nullptr);
    EXPECT_EQ(OnlyComponent<IrradianceVolumeComponent>(loaded)->Priority, -1);
}

TEST(SceneIntegerField, ANegativeNestedMemberRoundTripsThroughJsonAndBinary)
{
    ComponentSerializerRegistry serializers;
    RegisterComponent<SceneNestedSignedComponent>(serializers);
    Registry source;
    source.Components.RegisterComponent<SceneNestedSignedComponent>();
    const EntityId entity = source.Components.CreateEntity();
    source.Components.AddComponent(entity, SceneNestedSignedComponent{ .Inner = { .Offset = -1, .Small = -1 } });

    const JsonValue scene = SaveSceneJson(source, serializers);
    const JsonValue* component = ComponentOfFirstEntity(scene, "SceneNestedSigned");
    ASSERT_NE(component, nullptr);
    const JsonValue* inner = component->Find("inner");
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->Find("offset")->AsNumber(), -1.0);
    EXPECT_EQ(inner->Find("small")->AsNumber(), -1.0);

    Registry loaded;
    loaded.Components.RegisterComponent<SceneNestedSignedComponent>();
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneJson(scene, loaded, serializers, &error)) << error.Message;
    ASSERT_NE(OnlyComponent<SceneNestedSignedComponent>(loaded), nullptr);
    EXPECT_EQ(OnlyComponent<SceneNestedSignedComponent>(loaded)->Inner.Offset, -1);
    EXPECT_EQ(OnlyComponent<SceneNestedSignedComponent>(loaded)->Inner.Small, -1);

    Registry binary;
    binary.Components.RegisterComponent<SceneNestedSignedComponent>();
    const EntityId into = binary.Components.CreateEntity();
    ASSERT_TRUE(RoundTripsThroughBinary(serializers, "SceneNestedSigned", source, entity,
                                                                    binary, into));
    ASSERT_NE(OnlyComponent<SceneNestedSignedComponent>(binary), nullptr);
    EXPECT_EQ(OnlyComponent<SceneNestedSignedComponent>(binary)->Inner.Offset, -1);
    EXPECT_EQ(OnlyComponent<SceneNestedSignedComponent>(binary)->Inner.Small, -1);
}
