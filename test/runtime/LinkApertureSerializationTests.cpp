// LinkAperture and WorldLink's endpoints in a scene: they round trip, a
// default aperture is the documented size, and a world link written before
// links had endpoints reads as a demand-only link.

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    Registry EngineRegistry()
    {
        Registry registry;
        ComponentRegistrar components(registry.Components);
        RegisterEngineComponents(components);
        return registry;
    }

    ComponentSerializerRegistry EngineSerializers()
    {
        ComponentSerializerRegistry serializers;
        RegisterEngineSceneSerializers(serializers);
        return serializers;
    }

    template <typename T>
    const T* Only(const Registry& registry)
    {
        const T* found = nullptr;
        registry.Components.ForEachComponent<T>([&found](EntityId, const T& component) { found = &component; });
        return found;
    }
}

TEST(LinkApertureSerialization, ApertureAndEndpointsRoundTrip)
{
    const ComponentSerializerRegistry serializers = EngineSerializers();
    Registry source = EngineRegistry();
    const EntityId aperture = source.Components.CreateEntity();
    source.Components.AddComponent(aperture, LinkAperture{ Vec2d{ 0.75f, 1.25f } });
    const EntityId linkEntity = source.Components.CreateEntity();
    WorldLink link;
    link.Id = LinkId{ 0xe1 };
    link.ZoneA = ZoneId{ 0xa1 };
    link.ZoneB = ZoneId{ 0xa2 };
    link.Directions = DockDirectionBToA;
    link.EndpointA.Target = PersistentEntityId{ 0x10 };
    link.EndpointB.Target = PersistentEntityId{ 0x20 };
    source.Components.AddComponent(linkEntity, link);

    const auto parsed = JsonParse(JsonStringify(SaveSceneJson(source, serializers), true));
    ASSERT_TRUE(parsed.has_value());
    Registry loaded = EngineRegistry();
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));

    const LinkAperture* readAperture = Only<LinkAperture>(loaded);
    ASSERT_NE(readAperture, nullptr);
    EXPECT_EQ(readAperture->HalfExtents, (Vec2d{ 0.75f, 1.25f }));
    const WorldLink* readLink = Only<WorldLink>(loaded);
    ASSERT_NE(readLink, nullptr);
    EXPECT_EQ(readLink->Id, link.Id);
    EXPECT_EQ(readLink->Directions, DockDirectionBToA);
    EXPECT_EQ(readLink->EndpointA.Target, PersistentEntityId{ 0x10 });
    EXPECT_EQ(readLink->EndpointB.Target, PersistentEntityId{ 0x20 });
    EXPECT_FALSE(readLink->EndpointA.Entity.IsValid()) << "a scene holds the persistent id, never a live entity";
}

TEST(LinkApertureSerialization, DefaultsAreTheDocumentedSizeAndNoEndpoints)
{
    const LinkAperture aperture;
    EXPECT_EQ(aperture.HalfExtents, (Vec2d{ 1.0f, 2.0f }));
    const WorldLink link;
    EXPECT_FALSE(link.EndpointA.Target.IsValid());
    EXPECT_FALSE(link.EndpointB.Target.IsValid());

    const ComponentSerializerRegistry serializers = EngineSerializers();
    const auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [ { "components": { "LinkAperture": {} } } ],
        "hierarchy": []
    })");
    ASSERT_TRUE(parsed.has_value());
    Registry loaded = EngineRegistry();
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers));
    ASSERT_NE(Only<LinkAperture>(loaded), nullptr);
    EXPECT_EQ(Only<LinkAperture>(loaded)->HalfExtents, (Vec2d{ 1.0f, 2.0f }));
}

TEST(LinkApertureSerialization, AWorldLinkWithoutEndpointFieldsLoadsAsDemandOnly)
{
    const ComponentSerializerRegistry serializers = EngineSerializers();
    const auto parsed = JsonParse(R"({
        "version": 1,
        "entities": [
            {
                "components": {
                    "World Link": {
                        "id": "00000000000000e1",
                        "zone_a": "00000000000000a1",
                        "zone_b": "00000000000000a2",
                        "kind": "teleport",
                        "directions": 3
                    }
                }
            }
        ],
        "hierarchy": []
    })");
    ASSERT_TRUE(parsed.has_value());
    Registry loaded = EngineRegistry();
    SceneLoadError error;
    ASSERT_TRUE(LoadSceneJson(*parsed, loaded, serializers, &error)) << error.Message;
    const WorldLink* link = Only<WorldLink>(loaded);
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->Id, LinkId{ 0xe1 });
    EXPECT_EQ(link->ZoneB, ZoneId{ 0xa2 });
    EXPECT_FALSE(link->EndpointA.Target.IsValid());
    EXPECT_FALSE(link->EndpointB.Target.IsValid());
}
