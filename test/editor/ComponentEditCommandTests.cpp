// The two undoable component writes the inspector commits through, and the
// adapter registry that decides which components draw their own rows.
//
// RawComponentEditCommand and ValueCommand had no direct coverage: every test
// that exercised them did so through a tool or a gizmo, so a change to the byte
// path would have shown up as an unrelated failure or not at all.

#include "authoring/EditorComponentAdapter.h"
#include "authoring/GameplayVocabularyAdapters.h"
#include "authoring/HeldEditorAdapter.h"
#include "authoring/ResponsesEditorAdapter.h"
#include "authoring/WorldDockEditorAdapter.h"
#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EntityLabel.h"
#include "document/WorldDocument.h"
#include "document/commands/RawComponentEditCommand.h"
#include "document/commands/ValueCommand.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <abilities/AbilityGrants.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <core/logging/LoggingProvider.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/InventoryComponents.h>
#include <movement/components/CharacterMovement.h>
#include <world/held/Held.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <algorithm>
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
    EXPECT_TRUE(adapters.Register(MakeAbilityGrantsEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeCharacterMovementEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeResponsesEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeItemEntryEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeItemContainerEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeItemPickupEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeHeldEditorAdapter()));

    for (const ComponentTypeId type : {
             ResolveComponentTypeId<GameplayTagContainer>(),
             ResolveComponentTypeId<AttributeSet>(),
             ResolveComponentTypeId<AbilityGrants>(),
             ResolveComponentTypeId<CharacterMovement>(),
             ResolveComponentTypeId<ItemEntry>(),
             ResolveComponentTypeId<ItemContainer>(),
             ResolveComponentTypeId<ItemPickup>(),
             ResolveComponentTypeId<Held>(),
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

// A refused edit's note belongs to the entity in its document: the same id in
// another zone's document is another entity, and shows nothing.
TEST_F(ComponentEditCommandTest, ARefusalNoteStaysWithTheEntityInItsDocument)
{
    WorldDocument world(Logging);
    EditorDocument other(Logging);
    SelectionContext selectionContext;
    SelectionService selection{ selectionContext };
    other.SetRegistryIdentity({ 3, 1 }, ZoneId{ 7 });
    const EntityId here = Document.GetScene().CreateEntity(Vec3d::Zero());
    const EntityId there = other.GetScene().CreateEntity(Vec3d::Zero());
    ASSERT_EQ(here, there) << "the test needs the two documents' entities to share an id";
    Components().AddComponent<GameplayTagContainer>(here, GameplayTagContainer{});
    other.GetScene().GetRegistry().Components.AddComponent<GameplayTagContainer>(there, GameplayTagContainer{});

    const std::unique_ptr<IEditorComponentAdapter> tags = MakeGameplayTagEditorAdapter();
    EditorComponentInspectorContext refused{ .World = world, .Document = Document, .Scene = Document.GetScene(),
                                             .Selection = selection, .Commands = Commands, .Entity = here };
    EditorComponentInspectorContext elsewhere{ .World = world, .Document = other, .Scene = other.GetScene(),
                                               .Selection = selection, .Commands = Commands, .Entity = there };
    ASSERT_EQ(tags->SetInspectorValue(refused, "new", "Not..A.Tag"), PropertyEditResult::Refused);

    const auto notes = [&](const EditorComponentInspectorContext& context) {
        const std::vector<PropertyNode> nodes = tags->InspectorNodes(context);
        return std::ranges::count(nodes, std::string("refused"), &PropertyNode::Key);
    };
    EXPECT_EQ(notes(refused), 1);
    EXPECT_EQ(notes(elsewhere), 0);
}

TEST_F(ComponentEditCommandTest, UndoingAnInspectorEditPutsBackWhatTheEntityHad)
{
    WorldDocument world(Logging);
    SelectionContext selectionContext;
    SelectionService selection{ selectionContext };
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    Components().AddComponent<GameplayTagContainer>(entity, GameplayTagContainer{});

    const std::unique_ptr<IEditorComponentAdapter> tags = MakeGameplayTagEditorAdapter();
    EditorComponentInspectorContext context{ .World = world, .Document = Document, .Scene = Document.GetScene(),
                                             .Selection = selection, .Commands = Commands, .Entity = entity };
    ASSERT_EQ(tags->SetInspectorValue(context, "new", "Test.Undone"), PropertyEditResult::Applied);
    ASSERT_EQ(Components().TryGet<GameplayTagContainer>(entity)->Size(), 1u);

    Commands.Undo();
    EXPECT_EQ(Components().TryGet<GameplayTagContainer>(entity)->Size(), 0u) << "undo restored the edited value";
}

// An entry's item is chosen by its definition's asset, and the choice undoes.
TEST_F(ComponentEditCommandTest, AnEntrysItemIsChosenByNameAndUndone)
{
    WorldDocument world(Logging);
    SelectionContext selectionContext;
    SelectionService selection{ selectionContext };
    World& components = Components();
    ItemRegistry& items = components.GetResource<ItemRegistry>();
    const ItemId arrow = items.Reserve("asset://items/arrow.sdata");
    const ItemId bolt = items.Reserve("asset://items/bolt.sdata");
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    components.AddComponent<ItemEntry>(entity, ItemEntry{ .Definition = arrow, .Count = 5 });

    const std::unique_ptr<IEditorComponentAdapter> entries = MakeItemEntryEditorAdapter();
    EditorComponentInspectorContext context{ .World = world, .Document = Document, .Scene = Document.GetScene(),
                                             .Selection = selection, .Commands = Commands, .Entity = entity };
    ASSERT_EQ(entries->SetInspectorValue(context, "definition", "asset://items/bolt.sdata"), PropertyEditResult::Applied);
    EXPECT_EQ(components.TryGet<ItemEntry>(entity)->Definition, bolt);
    EXPECT_EQ(entries->SetInspectorValue(context, "definition", "asset://items/none.sdata"), PropertyEditResult::Refused);
    Commands.Undo();
    EXPECT_EQ(components.TryGet<ItemEntry>(entity)->Definition, arrow);

    ASSERT_EQ(entries->SetInspectorValue(context, "count", "12"), PropertyEditResult::Applied);
    EXPECT_EQ(components.TryGet<ItemEntry>(entity)->Count, 12u);
    Commands.Undo();
    EXPECT_EQ(components.TryGet<ItemEntry>(entity)->Count, 5u);
}

// "This chest holds 3 arrows" is authored by picking the chest as the entry's
// holder, among the document's entities, never itself or what it holds.
TEST_F(ComponentEditCommandTest, AHeldRecordsHolderIsPickedAmongTheDocumentsEntitiesAndUndone)
{
    WorldDocument world(Logging);
    SelectionContext selectionContext;
    SelectionService selection{ selectionContext };
    World& components = Components();
    const auto identified = [&](PersistentEntityId id) {
        const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
        if (components.HasComponent<PersistentIdComponent>(entity))
            components.TryGet<PersistentIdComponent>(entity)->Id = id;
        else
            components.AddComponent<PersistentIdComponent>(entity, PersistentIdComponent{ id });
        return entity;
    };
    const EntityId chest = identified(PersistentEntityId{ 0xC4E57 });
    const EntityId arrows = identified(PersistentEntityId{ 0xA770 });
    components.AddComponent<Held>(arrows, Held{});
    const EntityId inside = identified(PersistentEntityId{ 0x1D1E });
    components.AddComponent<Held>(inside, Held{ .Holder = { .Entity = arrows, .Target = PersistentEntityId{ 0xA770 } } });

    const std::unique_ptr<IEditorComponentAdapter> held = MakeHeldEditorAdapter();
    EditorComponentInspectorContext context{ .World = world, .Document = Document, .Scene = Document.GetScene(),
                                             .Selection = selection, .Commands = Commands, .Entity = arrows };
    const std::vector<PropertyNode> rows = held->InspectorNodes(context);
    ASSERT_FALSE(rows.empty());
    const std::vector<std::string>& choices = rows.front().Descriptor.Choices;
    const std::string chestLabel = EntityLabel(Document, chest) + " (00000000000c4e57)";
    EXPECT_EQ(choices.front(), "The world");
    EXPECT_NE(std::ranges::find(choices, chestLabel), choices.end());
    EXPECT_EQ(std::ranges::find(choices, EntityLabel(Document, arrows) + " (000000000000a770)"), choices.end());
    EXPECT_EQ(std::ranges::find(choices, EntityLabel(Document, inside) + " (0000000000001d1e)"), choices.end())
        << "a record cannot be held by what it holds";

    ASSERT_EQ(held->SetInspectorValue(context, "holder", chestLabel), PropertyEditResult::Applied);
    EXPECT_EQ(components.TryGet<Held>(arrows)->Holder.Target, PersistentEntityId{ 0xC4E57 });
    EXPECT_EQ(held->SetInspectorValue(context, "holder", "Nobody here"), PropertyEditResult::Refused);
    Commands.Undo();
    EXPECT_FALSE(components.TryGet<Held>(arrows)->Holder.Target.IsValid());
}
