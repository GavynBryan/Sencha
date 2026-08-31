// Byte-level golden for scene serialization.
//
// Round-trip tests prove a scene reads back as what it was; they do not prove
// the bytes on disk are unchanged. This one pins the exact JSON text a fixed
// scene produces, so a change to serializer ownership or field emission that
// would silently rewrite every serialized scene fails here first.
//
// A deliberate format change updates the constant; anything else that moves
// it is a defect. The scene below carries several component families on
// purpose, including a parented entity.

#include <gtest/gtest.h>

#include <core/hash/ContentHash.h>
#include <core/json/JsonStringify.h>
#include <math/geometry/3d/Transform3d.h>
#include <render/extract/Camera.h>
#include <render/PointLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include <cstddef>
#include <span>
#include <sstream>
#include <string>

namespace
{
    Registry MakeGoldenScene()
    {
        Registry registry;
        // The engine's own vocabulary, composed the way the runtime composes
        // it, so this fixture cannot know a different set of components than
        // the code it is testing.
        ComponentRegistrar components(registry.Components);
        RegisterEngineComponents(components);

        const EntityId root = registry.Components.CreateEntity();
        registry.Components.AddComponent(root, LocalTransform{
            Transform3f(Vec3d(1.0f, 2.0f, 3.0f), Quatf::Identity(), Vec3d::One()) });
        registry.Components.AddComponent(root, PointLightComponent{});

        const EntityId child = registry.Components.CreateEntity();
        registry.Components.AddComponent(child, LocalTransform{
            Transform3f(Vec3d(-4.0f, 0.5f, 7.25f), Quatf::Identity(),
                        Vec3d(2.0f, 2.0f, 2.0f)) });
        registry.Components.AddComponent(child, Parent{ root });
        registry.Components.AddComponent(child, CameraComponent{});

        return registry;
    }

    uint64_t HashOfString(const std::string& text)
    {
        return HashBytes64(std::string_view{ text });
    }

    ComponentSerializerRegistry EngineSerializers()
    {
        ComponentSerializerRegistry serializers;
        RegisterEngineSceneSerializers(serializers);
        return serializers;
    }
} // namespace

TEST(SceneSerializationGolden, JsonTextIsUnchanged)
{
    const ComponentSerializerRegistry serializers = EngineSerializers();
    Registry registry = MakeGoldenScene();
    const std::string json =
        JsonStringify(SaveSceneJson(registry, serializers), /*pretty*/ true);

    EXPECT_EQ(HashOfString(json), 0xf0f7a73e5baf9829ULL)
        << "scene JSON changed; if that was intended, update the constant.\n" << json;
}
