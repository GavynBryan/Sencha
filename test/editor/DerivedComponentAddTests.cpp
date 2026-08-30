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
#include "document/DerivedComponents.h"
#include "document/commands/RawComponentAddCommand.h"

#include <core/logging/LoggingProvider.h>
#include <movement/MovementComponents.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string_view>
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

// What the inspector's disclosure group shows: the components an entity carries
// that its file does not describe, and which component brought each one.
TEST_F(DerivedComponentAddTest, TheGroupNamesWhatTheFileDoesNotDescribe)
{
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    AddByMenu(entity, "CharacterMovement");
    // The world transform is seeded at the render-extraction boundary, which no
    // headless test reaches; seeding it here is what the scene does, and it is
    // the case of a derived column nothing declared it owed.
    SeedDerivedWorldTransform(Components(), entity);

    const std::vector<DerivedComponentRow> rows =
        DerivedComponentsOn(Components(), EditorSceneSerializers(), entity);

    const auto find = [&](std::string_view name) -> const DerivedComponentRow*
    {
        for (const DerivedComponentRow& row : rows)
            if (row.Name == name)
                return &row;
        return nullptr;
    };

    const ComponentId movement =
        Components().GetComponentId<CharacterMovement>();

    // Owed, and attributed to the component that declared it.
    const DerivedComponentRow* support = find("sencha.support_state");
    ASSERT_NE(support, nullptr);
    EXPECT_EQ(support->ProvidedBy, movement);
    const DerivedComponentRow* request = find("sencha.motion_request");
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->ProvidedBy, movement);

    // Derived but not owed: seeded from the local transform every frame, so it
    // is on the entity with nothing declaring it.
    const DerivedComponentRow* worldTransform = find("sencha.world_transform");
    ASSERT_NE(worldTransform, nullptr);
    EXPECT_EQ(worldTransform->ProvidedBy, InvalidComponentId);

    // Authored components stay in the rows above the group, including the one
    // that brought everything here.
    EXPECT_EQ(find("CharacterMovement"), nullptr);
    EXPECT_EQ(find("sencha.local_transform"), nullptr);
    EXPECT_EQ(find("Transform"), nullptr);
    for (const DerivedComponentRow& row : rows)
    {
        const ComponentMeta* meta = Components().GetMeta(row.Id);
        ASSERT_NE(meta, nullptr);
        EXPECT_EQ(EditorSceneSerializers().FindByType(meta->TypeId), nullptr)
            << row.Name << " is authored and does not belong in this group";
    }
}

TEST_F(DerivedComponentAddTest, TheGroupIsOrderedAndStable)
{
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    AddByMenu(entity, "CharacterMovement");

    const std::vector<DerivedComponentRow> first =
        DerivedComponentsOn(Components(), EditorSceneSerializers(), entity);
    ASSERT_FALSE(first.empty());
    EXPECT_TRUE(std::is_sorted(first.begin(), first.end(),
                               [](const DerivedComponentRow& a,
                                  const DerivedComponentRow& b)
                               { return a.Id < b.Id; }));

    // Same list twice running, which is what lets the panel recompute it every
    // frame instead of caching it behind an invalidation rule.
    const std::vector<DerivedComponentRow> second =
        DerivedComponentsOn(Components(), EditorSceneSerializers(), entity);
    ASSERT_EQ(second.size(), first.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_EQ(second[i].Id, first[i].Id);
        EXPECT_EQ(second[i].ProvidedBy, first[i].ProvidedBy);
    }

    // A fresh entity carries a local transform, an identity and a name, all of
    // them authored, so it has no group at all and none is drawn.
    const EntityId bare = Document.GetScene().CreateEntity(Vec3d::Zero());
    EXPECT_TRUE(DerivedComponentsOn(Components(), EditorSceneSerializers(), bare).empty());
}
