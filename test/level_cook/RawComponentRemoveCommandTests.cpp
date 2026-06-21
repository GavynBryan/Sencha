// RawComponentRemoveCommand: removing a component from an entity must be
// undoable, restoring the component's full state via the serializer round-trip.
// The capture is registry-driven (any registered component), exercised here on
// CameraComponent, which needs no asset system; the asset-bearing
// StaticMeshComponent path is covered at runtime.

#include "level/LevelDocument.h"
#include "level/LevelScene.h"
#include "level/LevelSerialization.h"
#include "level/commands/RawComponentRemoveCommand.h"

#include <core/json/JsonStringify.h>
#include <core/logging/LoggingProvider.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
    class RawComponentRemoveCommandTest : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite() { RegisterLevelSerializers(); }

        // The serializer for a given JSON key (process-global, stable).
        IComponentSerializer* SerializerFor(std::string_view jsonKey) const
        {
            for (const auto& entry : GetComponentSerializerEntries())
                if (entry->JsonKey() == jsonKey)
                    return entry.get();
            return nullptr;
        }

        bool HasComponent(EntityId entity, const IComponentSerializer& serializer) const
        {
            return serializer.HasComponent(entity, Scene.GetRegistry());
        }

        std::string Capture(EntityId entity, const IComponentSerializer& serializer) const
        {
            return JsonStringify(Document.CaptureComponent(entity, serializer), true);
        }

        LoggingProvider Logging;
        LevelDocument   Document{ Logging };
        LevelScene&     Scene = Document.GetScene();
    };

    TEST_F(RawComponentRemoveCommandTest, RemovesAndRestoresComponent)
    {
        IComponentSerializer* camera = SerializerFor("Camera");
        ASSERT_NE(camera, nullptr);

        const EntityId entity = Scene.CreateCamera(Vec3d{ 1.0, 2.0, 3.0 });
        ASSERT_TRUE(HasComponent(entity, *camera));
        const std::string before = Capture(entity, *camera);

        RawComponentRemoveCommand command(entity, *camera, Scene, Document);

        command.Execute();
        EXPECT_FALSE(HasComponent(entity, *camera));
        // The entity itself survives; only the component is gone.
        EXPECT_TRUE(Scene.HasEntity(entity));

        command.Undo();
        ASSERT_TRUE(HasComponent(entity, *camera));
        EXPECT_EQ(Capture(entity, *camera), before);

        // Redo removes it again (entity id is stable across the round-trip).
        command.Execute();
        EXPECT_FALSE(HasComponent(entity, *camera));
    }

    TEST_F(RawComponentRemoveCommandTest, TransformIsNotRemovable)
    {
        // The structural transform opts out of removal; ordinary components do not.
        // Queried through the serializer, so no component type is named here.
        const IComponentSerializer* transform = SerializerFor("Transform");
        const IComponentSerializer* camera = SerializerFor("Camera");
        ASSERT_NE(transform, nullptr);
        ASSERT_NE(camera, nullptr);

        EXPECT_FALSE(transform->IsRemovable());
        EXPECT_TRUE(camera->IsRemovable());
    }
} // namespace
