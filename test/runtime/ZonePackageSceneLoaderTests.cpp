#include <core/logging/LoggingProvider.h>
#include <ecs/WorldComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializer.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneFormat.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>
#include <world/build/EntityBuildPackage.h>
#include <zone/ZonePackageImporter.h>
#include <zone/ZonePackageSceneLoader.h>

#include <gtest/gtest.h>

#include <memory>

struct ScenePackageValue
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(
    ScenePackageValue,
    "test.scene_package_value");

template <>
struct TypeSchema<ScenePackageValue>
{
    static constexpr std::string_view Name = "scene_package_value";
    static constexpr std::uint32_t SceneChunkId =
        MakeFourCC('S', 'P', 'K', 'G');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &ScenePackageValue::Value),
        };
    }
};

namespace
{
ComponentSerializerRegistry MakeSerializers()
{
    ComponentSerializerRegistry serializers;
    const auto result = serializers.Register(
        std::make_unique<ComponentSerializer<ScenePackageValue>>());
    EXPECT_EQ(
        result,
        ComponentSerializerRegistry::RegisterResult::Added);
    return serializers;
}

WorldComponentSchema MakeSchema()
{
    WorldComponentSchema schema;
    schema.Add<Parent>();
    schema.Add<ScenePackageValue>();
    schema.Seal();
    return schema;
}

JsonValue MakeSceneJson()
{
    JsonValue::Array entities;
    entities.emplace_back(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{
            { "scene_package_value", JsonValue(JsonValue::Object{
                { "value", JsonValue(11.0) },
            }) },
        }) },
    });
    entities.emplace_back(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{
            { "scene_package_value", JsonValue(JsonValue::Object{
                { "value", JsonValue(22.0) },
            }) },
        }) },
    });

    JsonValue::Array hierarchy;
    hierarchy.emplace_back(JsonValue::Object{
        { "child", JsonValue(1.0) },
        { "parent", JsonValue(0.0) },
    });

    return JsonValue(JsonValue::Object{
        { "version", JsonValue(static_cast<double>(SceneVersion)) },
        { "entities", JsonValue(std::move(entities)) },
        { "hierarchy", JsonValue(std::move(hierarchy)) },
    });
}
} // namespace

TEST(ZonePackageSceneLoader, ParsesOnWorkerShapeAndDecodesOnOwnerThread)
{
    ComponentSerializerRegistry serializers = MakeSerializers();
    const WorldComponentSchema schema = MakeSchema();
    RuntimeWorld runtime(schema);

    EntityBuildPackage package;
    SceneLoadError buildError;
    ASSERT_TRUE(BuildEntityPackageFromSceneJson(
        MakeSceneJson(),
        serializers,
        package,
        &buildError)) << buildError.Message;
    ASSERT_EQ(package.EntityCount(), 2u);
    ASSERT_EQ(package.Parents().size(), 1u);

    LoggingProvider logging;
    SceneSerializationContext context(logging);
    ZoneImportError importError;
    ASSERT_TRUE(ImportZonePackage(
        runtime,
        schema,
        ZoneId{ 40 },
        package,
        serializers,
        context,
        ZoneParticipation{ .Logic = true },
        &importError)) << importError.Message;

    RuntimeZoneRecord* zone = runtime.FindZone(ZoneId{ 40 });
    ASSERT_NE(zone, nullptr);

    EntityId root;
    EntityId child;
    for (EntityId entity : runtime.Entities().GetAliveEntities())
    {
        if (runtime.Entities().GetEntityPartition(entity) != zone->Partition)
            continue;
        const ScenePackageValue* value =
            runtime.Entities().TryGet<ScenePackageValue>(entity);
        ASSERT_NE(value, nullptr);
        if (value->Value == 11)
            root = entity;
        else if (value->Value == 22)
            child = entity;
    }

    ASSERT_TRUE(root.IsValid());
    ASSERT_TRUE(child.IsValid());
    const Parent* parent = runtime.Entities().TryGet<Parent>(child);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(parent->Entity, root);
}

TEST(ZonePackageSceneLoader, UnknownSerializedComponentRejectsWholePackage)
{
    ComponentSerializerRegistry serializers = MakeSerializers();
    EntityBuildPackage package;

    JsonValue scene(JsonValue::Object{
        { "version", JsonValue(static_cast<double>(SceneVersion)) },
        { "entities", JsonValue(JsonValue::Array{
            JsonValue(JsonValue::Object{
                { "components", JsonValue(JsonValue::Object{
                    { "missing_component", JsonValue(JsonValue::Object{}) },
                }) },
            }),
        }) },
        { "hierarchy", JsonValue(JsonValue::Array{}) },
    });

    SceneLoadError error;
    EXPECT_FALSE(BuildEntityPackageFromSceneJson(
        scene,
        serializers,
        package,
        &error));
    EXPECT_FALSE(error.Message.empty());
    EXPECT_TRUE(package.Empty());
}

TEST(ZonePackageSceneLoader, LiftsPersistentIdIntoPackageMetadata)
{
    ComponentSerializerRegistry serializers;
    ASSERT_EQ(
        serializers.Register(
            std::make_unique<ComponentSerializer<PersistentIdComponent>>()),
        ComponentSerializerRegistry::RegisterResult::Added);

    JsonValue::Array entities;
    entities.emplace_back(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{
            { "persistent_id", JsonValue(JsonValue::Object{
                { "id", JsonValue(std::string("00000000000000aa")) },
            }) },
        }) },
    });
    entities.emplace_back(JsonValue::Object{
        { "components", JsonValue(JsonValue::Object{}) },
    });
    const JsonValue root(JsonValue::Object{
        { "version", JsonValue(static_cast<double>(SceneVersion)) },
        { "entities", JsonValue(std::move(entities)) },
    });

    EntityBuildPackage package;
    SceneLoadError error;
    ASSERT_TRUE(BuildEntityPackageFromSceneJson(root, serializers, package, &error))
        << error.Message;
    ASSERT_EQ(package.EntityCount(), 2u);
    EXPECT_EQ(package.Entities()[0].PersistentId, (PersistentEntityId{ 0xaa }));
    EXPECT_FALSE(package.Entities()[1].PersistentId.IsValid());
}
