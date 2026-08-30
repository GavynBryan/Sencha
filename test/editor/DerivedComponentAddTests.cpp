// Adding a component in the inspector, and taking the add back.
//
// The menu is driven by the serializer registry and never names a C++ type, so
// it reaches the World through the type-erased add. That used to mean a
// hand-built pawn and a loaded one were different entities: the loaded one came
// through a serializer's typed add and got what CharacterMovement owes, and the
// hand-built one did not. Both routes now provision, which makes the undo a
// diff -- one gesture can put twelve components on an entity, and taking back
// only the one that was named would strand the rest.

#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/commands/RawComponentAddCommand.h"

#include <core/logging/LoggingProvider.h>
#include <movement/MovementComponents.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/transform/TransformHistory.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
class DerivedComponentAddTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    DerivedComponentAddTest()
        : Document(Logging)
    {
    }

    [[nodiscard]] World& Components()
    {
        return Document.GetScene().GetRegistry().Components;
    }

    // The inspector's own gesture: a component chosen by identity, with the
    // serializer's defaults for bytes.
    void AddByMenu(EntityId entity, std::string_view jsonKey)
    {
        const IComponentSerializer* serializer =
            EditorSceneSerializers().FindByJsonKey(jsonKey);
        ASSERT_NE(serializer, nullptr) << jsonKey;
        const ComponentId id =
            Components().GetComponentIdByType(serializer->TypeId());
        ASSERT_NE(id, InvalidComponentId) << jsonKey;
        Commands.Execute(std::make_unique<RawComponentAddCommand>(
            entity, id, serializer->DefaultBytes(), Document.GetScene(), Document));
    }

    template <typename T>
    [[nodiscard]] bool Carries(EntityId entity)
    {
        return Components().IsRegistered<T>()
            && Components().HasComponent<T>(entity);
    }

    LoggingProvider Logging;
    EditorDocument  Document;
    CommandStack    Commands;
};
}

TEST_F(DerivedComponentAddTest, AddingAComponentBringsWhatItOwes)
{
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    ASSERT_FALSE(Carries<MovementIntent>(entity));

    AddByMenu(entity, "CharacterMovement");

    EXPECT_TRUE(Carries<CharacterMovement>(entity));
    EXPECT_TRUE(Carries<MovementIntent>(entity));
    EXPECT_TRUE(Carries<JumpState>(entity));
    EXPECT_TRUE(Carries<KinematicState>(entity));
    EXPECT_TRUE(Carries<SupportState>(entity));
    EXPECT_TRUE(Carries<ResolvedMovementTuning>(entity));
    EXPECT_TRUE(Carries<LocomotionOutput>(entity));
    EXPECT_TRUE(Carries<MotionAxisOverride>(entity));
    EXPECT_TRUE(Carries<MotionImpulse>(entity));
    EXPECT_TRUE(Carries<MotionRequest>(entity));
    EXPECT_TRUE(Carries<ModeTransitionRequest>(entity));
    // The presentation column too: a document that registered a smaller
    // vocabulary than the runtime would silently skip this one.
    EXPECT_TRUE(Carries<WorldTransformHistory>(entity));

    // At their initializers, not zeroed -- the provision goes through the typed
    // add, so a member initializer is what a provisioned column holds.
    const ResolvedMovementTuning* tuning =
        Components().TryGet<ResolvedMovementTuning>(entity);
    ASSERT_NE(tuning, nullptr);
    EXPECT_FLOAT_EQ(tuning->GravityScale, ResolvedMovementTuning{}.GravityScale);
}

TEST_F(DerivedComponentAddTest, UndoTakesBackWhatTheAddBrought)
{
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());

    World& world = Components();
    std::vector<ComponentId> before;
    world.ComponentIdsOn(entity, before);

    AddByMenu(entity, "CharacterMovement");
    std::vector<ComponentId> added;
    world.ComponentIdsOn(entity, added);
    ASSERT_GT(added.size(), before.size() + 1)
        << "the add brought nothing beyond the component that was named";

    Commands.Undo();
    std::vector<ComponentId> undone;
    world.ComponentIdsOn(entity, undone);
    EXPECT_EQ(undone, before);

    // And redo puts back exactly what it took.
    Commands.Redo();
    std::vector<ComponentId> redone;
    world.ComponentIdsOn(entity, redone);
    EXPECT_EQ(redone, added);
}

// The other half of the same rule: an undo takes back what its own add brought,
// and nothing else. A component the entity already carried survives.
TEST_F(DerivedComponentAddTest, UndoLeavesWhatTheEntityAlreadyCarried)
{
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    World& world = Components();
    world.AddComponent<MovementIntent>(entity, MovementIntent{});

    std::vector<ComponentId> before;
    world.ComponentIdsOn(entity, before);

    AddByMenu(entity, "CharacterMovement");
    Commands.Undo();

    std::vector<ComponentId> undone;
    world.ComponentIdsOn(entity, undone);
    EXPECT_EQ(undone, before);
    EXPECT_TRUE(Carries<MovementIntent>(entity));
    EXPECT_FALSE(Carries<CharacterMovement>(entity));
    EXPECT_FALSE(Carries<SupportState>(entity));
}
