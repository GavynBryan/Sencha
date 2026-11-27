// The two undoable component writes the inspector commits through, and the
// adapter registry that decides which components draw their own rows.
//
// RawComponentEditCommand and ValueCommand had no direct coverage: every test
// that exercised them did so through a tool or a gizmo, so a change to the byte
// path would have shown up as an unrelated failure or not at all.

#include "authoring/EditorComponentAdapter.h"
#include "authoring/GameplayVocabularyAdapters.h"
#include "authoring/EquipmentStateEditorAdapter.h"
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
#include <equipment/EquipmentComponents.h>
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
    ASSERT_EQ(after.Grant(stunned, 2), GameplayTagWrite::Added);

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
    EXPECT_TRUE(adapters.Register(MakeResponsesEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeItemEntryEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeHeldEditorAdapter()));
    EXPECT_TRUE(adapters.Register(MakeEquipmentStateEditorAdapter()));

    for (const ComponentTypeId type : {
             ResolveComponentTypeId<GameplayTagContainer>(),
             ResolveComponentTypeId<AttributeSet>(),
             ResolveComponentTypeId<AbilityGrants>(),
             ResolveComponentTypeId<ItemEntry>(),
             ResolveComponentTypeId<Held>(),
             ResolveComponentTypeId<EquipmentState>(),
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
        if (!serializer->RuntimeFields().empty() || !serializer->Authorable())
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

TEST_F(ComponentEditCommandTest, AnEntrysCountStaysWithinWhatItsStackHolds)
{
    WorldDocument world(Logging);
    SelectionContext selectionContext;
    SelectionService selection{ selectionContext };
    World& components = Components();
    ItemRegistry& items = components.GetResource<ItemRegistry>();
    const ItemId arrow = items.Define("asset://items/arrow.sdata", ItemDefinition{ .Name = "Arrow", .StackMax = 20 });
    (void)items.Define("asset://items/cloak.sdata", ItemDefinition{ .Name = "Cloak" });
    const EntityId entity = Document.GetScene().CreateEntity(Vec3d::Zero());
    components.AddComponent<ItemEntry>(entity, ItemEntry{ .Definition = arrow, .Count = 5 });

    const std::unique_ptr<IEditorComponentAdapter> entries = MakeItemEntryEditorAdapter();
    EditorComponentInspectorContext context{ .World = world, .Document = Document, .Scene = Document.GetScene(),
                                             .Selection = selection, .Commands = Commands, .Entity = entity };
    EXPECT_EQ(entries->SetInspectorValue(context, "count", "21"), PropertyEditResult::Refused);
    EXPECT_EQ(entries->SetInspectorValue(context, "count", "0"), PropertyEditResult::Refused);
    ASSERT_EQ(entries->SetInspectorValue(context, "count", "20"), PropertyEditResult::Applied);
    EXPECT_EQ(entries->SetInspectorValue(context, "definition", "asset://items/cloak.sdata"), PropertyEditResult::Refused)
        << "twenty cloaks are twenty entries";
    EXPECT_EQ(components.TryGet<ItemEntry>(entity)->Count, 20u);
    EXPECT_EQ(components.TryGet<ItemEntry>(entity)->Definition, arrow);
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

// A guard's sword is authored by choosing, for a slot, an entry the guard holds.
TEST_F(ComponentEditCommandTest, AWearersSlotIsFilledFromWhatItHoldsAndUndone)
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
    EquipmentLayout body;
    for (const char* key : { "hand.main", "hand.off", "ring" })
    {
        body.Slots.emplace_back();
        body.Slots.back().Key = key;
    }
    const EquipmentLayoutId layout =
        components.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/body.sdata", std::move(body));
    ItemRegistry& items = components.GetResource<ItemRegistry>();
    const ItemId sword = items.Define("asset://items/sword.sdata", ItemDefinition{ .Name = "Sword", .Occupies = { "hand" } });
    const ItemId greatsword = items.Define("asset://items/greatsword.sdata",
                                           ItemDefinition{ .Name = "Greatsword", .Occupies = { "hand.main", "hand.off" } });
    const ItemId ring = items.Define("asset://items/ring.sdata", ItemDefinition{ .Name = "Ring", .Occupies = { "ring" } });

    const EntityId guard = identified(PersistentEntityId{ 0x60A2D });
    components.AddComponent<EquipmentState>(guard, EquipmentState{ .Layout = layout, .Count = 0, .NextKey = 1, .Records = {} });
    const auto heldBy = [&](EntityId holder, PersistentEntityId holderId, PersistentEntityId id, ItemId item) {
        const EntityId entry = identified(id);
        components.AddComponent<ItemEntry>(entry, ItemEntry{ .Definition = item, .Count = 1 });
        components.AddComponent<Held>(entry, Held{ .Holder = { .Entity = holder, .Target = holderId } });
        return entry;
    };
    (void)heldBy(guard, PersistentEntityId{ 0x60A2D }, PersistentEntityId{ 0x5A0D }, sword);
    const EntityId great = heldBy(guard, PersistentEntityId{ 0x60A2D }, PersistentEntityId{ 0x62EA7 }, greatsword);
    (void)heldBy(guard, PersistentEntityId{ 0x60A2D }, PersistentEntityId{ 0x2126 }, ring);
    const EntityId thief = identified(PersistentEntityId{ 0x7E1EF });
    const EntityId stolen = heldBy(thief, PersistentEntityId{ 0x7E1EF }, PersistentEntityId{ 0x5701E }, sword);

    const std::unique_ptr<IEditorComponentAdapter> equipment = MakeEquipmentStateEditorAdapter();
    EditorComponentInspectorContext context{ .World = world, .Document = Document, .Scene = Document.GetScene(),
                                             .Selection = selection, .Commands = Commands, .Entity = guard };
    const auto row = [&](std::string_view key) {
        const std::vector<PropertyNode> rows = equipment->InspectorNodes(context);
        const auto found = std::ranges::find(rows, key, &PropertyNode::Key);
        EXPECT_NE(found, rows.end()) << key;
        return found != rows.end() ? found->Descriptor : PropertyDescriptor{};
    };
    const std::vector<std::string> offered = row("slot:hand.main").Choices;
    EXPECT_EQ(offered.front(), "Empty");
    EXPECT_EQ(offered.size(), 3u) << "the sword and the greatsword, not the ring";
    EXPECT_EQ(std::ranges::find(offered, EntityLabel(Document, stolen) + " (000000000005701e)"), offered.end())
        << "what another holds is not this wearer's to wear";

    const auto state = [&]() -> const EquipmentState& { return *std::as_const(components).TryGet<EquipmentState>(guard); };
    const auto worn = [&] { return EquipmentRecordsOf(components, guard); };
    ASSERT_EQ(equipment->SetInspectorValue(context, "slot:hand.main", offered[1]), PropertyEditResult::Applied);
    ASSERT_EQ(state().Count, 1u);
    EXPECT_EQ(worn()[0].Slots, 1u);
    EXPECT_EQ(worn()[0].Derivation, EquipmentDerivation::Pending);
    EXPECT_EQ(equipment->SetInspectorValue(context, "slot:hand.main", offered[1]), PropertyEditResult::Unchanged);
    EXPECT_EQ(equipment->SetInspectorValue(context, "slot:hand.main", "Nobody's"), PropertyEditResult::Refused);
    EXPECT_EQ(equipment->SetInspectorValue(context, "layout", "asset://equipment/other.sdata"), PropertyEditResult::Refused);

    const std::string greatLabel = EntityLabel(Document, great) + " (0000000000062ea7)";
    ASSERT_EQ(equipment->SetInspectorValue(context, "slot:hand.main", greatLabel), PropertyEditResult::Applied);
    ASSERT_EQ(state().Count, 1u) << "it replaces what held its slots";
    EXPECT_EQ(worn()[0].Slots, 3u);
    EXPECT_EQ(worn()[0].Source.Target, PersistentEntityId{ 0x62EA7 });
    EXPECT_EQ(worn()[0].Key, 2u) << "a key is never reused";

    ASSERT_EQ(equipment->SetInspectorValue(context, "slot:hand.off", "Empty"), PropertyEditResult::Applied);
    EXPECT_EQ(state().Count, 0u);
    Commands.Undo();
    EXPECT_EQ(worn()[0].Source.Target, PersistentEntityId{ 0x62EA7 });
}
