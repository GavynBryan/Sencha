// The two undoable component writes the inspector commits through, and the
// adapter registry that decides which components draw their own rows.
//
// RawComponentEditCommand and ValueCommand had no direct coverage: every test
// that exercised them did so through a tool or a gizmo, so a change to the byte
// path would have shown up as an unrelated failure or not at all.

#include "authoring/EditorComponentAdapter.h"
#include "authoring/GameplayVocabularyAdapters.h"
#include "authoring/WorldDockEditorAdapter.h"
#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/commands/RawComponentEditCommand.h"
#include "document/commands/ValueCommand.h"

#include <abilities/AbilitySet.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/logging/LoggingProvider.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/components/CharacterMovement.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

namespace
{
class ComponentEditCommandTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    ComponentEditCommandTest()
        : Document(Logging)
    {
    }

    [[nodiscard]] World& Components()
    {
        return Document.GetScene().GetRegistry().Components;
    }

    LoggingProvider Logging;
    EditorDocument  Document;
    CommandStack    Commands;
};
}

// A whole-component value written through ValueCommand, which is what the
// vocabulary adapters commit: undo restores the tags the entity had, redo puts
// the granted one back.
TEST_F(ComponentEditCommandTest, ValueCommandRoundTripsAWholeComponent)
{
    World& world = Components();
    auto* tags = world.TryGetResource<GameplayTagRegistry>();
    ASSERT_NE(tags, nullptr);
    const GameplayTagId stunned = *tags->RegisterTag("State.Stunned");

    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    world.AddComponent<GameplayTagContainer>(entity, GameplayTagContainer{});

    const GameplayTagContainer before = *world.TryGet<GameplayTagContainer>(entity);
    GameplayTagContainer after = before;
    ASSERT_TRUE(after.Grant(stunned, 2));

    Commands.Execute(std::make_unique<ValueCommand<GameplayTagContainer>>(
        before, after,
        [&scene = Document.GetScene(), entity](const GameplayTagContainer& value)
        { scene.SetComponent(entity, value); },
        Document));

    EXPECT_EQ(world.TryGet<GameplayTagContainer>(entity)->StackCount(stunned), 2u);
    Commands.Undo();
    EXPECT_FALSE(world.TryGet<GameplayTagContainer>(entity)->HasExact(stunned));
    Commands.Redo();
    EXPECT_EQ(world.TryGet<GameplayTagContainer>(entity)->StackCount(stunned), 2u);
}

// The byte path the generic inspector rows use.
TEST_F(ComponentEditCommandTest, RawComponentEditCommandRoundTripsBytes)
{
    World& world = Components();
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    const ComponentId id =
        world.GetComponentIdByType(ResolveComponentTypeId<LocalTransform>());
    ASSERT_NE(id, InvalidComponentId);

    const std::size_t size = world.GetMeta(id)->Size;
    std::vector<std::byte> before(size);
    std::memcpy(before.data(), world.GetComponentRaw(entity, id), size);

    LocalTransform moved = *world.TryGet<LocalTransform>(entity);
    moved.Value.Position = Vec3d(3.0f, 4.0f, 5.0f);
    std::vector<std::byte> after(size);
    std::memcpy(after.data(), &moved, size);

    Commands.Execute(std::make_unique<RawComponentEditCommand>(
        entity, id, before, after, Document.GetScene(), Document));

    EXPECT_EQ(world.TryGet<LocalTransform>(entity)->Value.Position, Vec3d(3.0f, 4.0f, 5.0f));
    Commands.Undo();
    EXPECT_EQ(world.TryGet<LocalTransform>(entity)->Value.Position, Vec3d::Zero());
    Commands.Redo();
    EXPECT_EQ(world.TryGet<LocalTransform>(entity)->Value.Position, Vec3d(3.0f, 4.0f, 5.0f));
}

// Each adapter must resolve to a component the serializer registry also knows:
// the affordance pass pairs the two, and an adapter for a component nothing
// serializes would never be reached.
TEST_F(ComponentEditCommandTest, EveryVocabularyAdapterPairsWithItsSerializer)
{
    EditorComponentAdapterRegistry adapters;
    EXPECT_TRUE(adapters.Register(MakeWorldDockEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeGameplayTagEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeAttributeSetEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeAbilitySetEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeCharacterMovementEditorAdapter()));

    for (const ComponentTypeId type : {
             ResolveComponentTypeId<GameplayTagContainer>(),
             ResolveComponentTypeId<AttributeSet>(),
             ResolveComponentTypeId<AbilitySet>(),
             ResolveComponentTypeId<CharacterMovement>(),
         })
    {
        EXPECT_NE(adapters.Find(type), nullptr);
        EXPECT_NE(EditorSceneSerializers().FindByType(type), nullptr);
    }

    // Nothing with state is left as a bare header: a component that describes
    // no fields must draw its own rows. A zero-size tag is exempt -- carrying
    // it is the whole of what it says, so the header is the value.
    World& world = Components();
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        if (!serializer->RuntimeFields().empty())
            continue;
        const ComponentId id = world.GetComponentIdByType(serializer->TypeId());
        const ComponentMeta* meta = id != InvalidComponentId ? world.GetMeta(id) : nullptr;
        if (meta == nullptr || meta->Size == 0)
            continue;
        EXPECT_NE(adapters.Find(serializer->TypeId()), nullptr)
            << serializer->JsonKey() << " draws neither fields nor its own rows";
    }
}
