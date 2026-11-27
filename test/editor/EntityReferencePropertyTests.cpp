#include <gtest/gtest.h>

#include "authoring/ComponentPropertySource.h"
#include "authoring/EditorComponentAdapter.h"
#include "authoring/EntityReferences.h"
#include "authoring/HeldEditorAdapter.h"
#include "authoring/ResponsesEditorAdapter.h"
#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/EntityNameComponent.h"
#include "document/WorldDocument.h"
#include "properties/PropertyTree.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <world/held/Held.h>
#include <world/identity/PersistentIdComponent.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

// A reference to another entity, picked by name among the entities its field
// admits in every loaded document of the world, and read back as where its
// target is -- loaded, in a zone that is not, or nowhere.

namespace
{
// A world of three zones: Cellar focused, Attic open beside it, Vault closed.
class EntityReferenceTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { RegisterDocumentSerializers(); }

    void SetUp() override
    {
        World.NewWorld("References");
        Cellar = World.Manifest().Zones[0].Id;
        ASSERT_TRUE(World.RenameZone(Cellar, "Cellar"));
        const GraphId graph = World.Manifest().Graphs[0].Id;
        Attic = World.AddZone(graph, "Attic");
        ASSERT_TRUE(World.LoadZone(Attic));
        Vault = World.AddZone(graph, "Vault");
        ASSERT_EQ(World.FocusZone(), Cellar);
    }

    [[nodiscard]] EditorDocument& CellarDocument() { return World.FocusDocument(); }
    [[nodiscard]] EditorDocument& AtticDocument() { return *World.ZoneDocument(Attic); }

    struct Placed
    {
        EntityId Entity;
        std::string Value;
    };

    static Placed Place(EditorDocument& document, const std::string& name)
    {
        EditorScene& scene = document.GetScene();
        const EntityId entity = scene.CreateEntity(Vec3d::Zero());
        scene.GetRegistry().Components.TryGet<EntityNameComponent>(entity)->Value = InlineString<64>(name);
        return { entity, PersistentEntityIdToString(scene.PersistentIdOf(entity)) };
    }

    static const EntityReferenceChoice* Offered(const std::vector<EntityReferenceChoice>& choices,
                                                const std::string& value)
    {
        const auto found = std::ranges::find(choices, value, &EntityReferenceChoice::Value);
        return found != choices.end() ? &*found : nullptr;
    }

    static bool OffersVerb(const PropertyNode& node, std::string_view verb)
    {
        return std::ranges::find(node.Descriptor.Verbs, verb, &PropertyVerb::Id) != node.Descriptor.Verbs.end();
    }

    LoggingProvider Logging;
    WorldDocument World{ Logging };
    ZoneId Cellar;
    ZoneId Attic;
    ZoneId Vault;
};
} // namespace

TEST_F(EntityReferenceTest, CandidatesComeFromEveryLoadedDocumentByNameAndPlace)
{
    const Placed chest = Place(CellarDocument(), "Chest");
    const Placed lamp = Place(AtticDocument(), "Lamp");
    const Placed beacon = Place(World.WorldSceneDocument(), "Beacon");

    const std::vector<EntityReferenceChoice> choices = EntityReferenceCandidates(World, {});
    const EntityReferenceChoice* offeredChest = Offered(choices, chest.Value);
    const EntityReferenceChoice* offeredLamp = Offered(choices, lamp.Value);
    const EntityReferenceChoice* offeredBeacon = Offered(choices, beacon.Value);
    ASSERT_NE(offeredChest, nullptr);
    ASSERT_NE(offeredLamp, nullptr) << "a document open beside the focus was not offered";
    ASSERT_NE(offeredBeacon, nullptr) << "the world scene was not offered";
    EXPECT_EQ(offeredChest->Label, "Chest");
    EXPECT_EQ(offeredChest->Place, "Cellar");
    EXPECT_EQ(offeredLamp->Label, "Lamp");
    EXPECT_EQ(offeredLamp->Place, "Attic");
    EXPECT_EQ(offeredBeacon->Place, "World scene");
    EXPECT_TRUE(std::ranges::is_sorted(choices, {}, &EntityReferenceChoice::Label));
}

// The shape a link's endpoint field supplies: entities carrying a component,
// less those another holder already claims.
TEST_F(EntityReferenceTest, AFilterNarrowsWhatIsOfferedAndWhatAPickMayName)
{
    const Placed chest = Place(CellarDocument(), "Chest");
    const Placed crate = Place(CellarDocument(), "Crate");
    const Placed lamp = Place(AtticDocument(), "Lamp");
    const Placed lantern = Place(AtticDocument(), "Lantern");
    CellarDocument().GetScene().GetRegistry().Components.AddComponent<GameplayTagContainer>(chest.Entity, {});
    AtticDocument().GetScene().GetRegistry().Components.AddComponent<GameplayTagContainer>(lamp.Entity, {});
    AtticDocument().GetScene().GetRegistry().Components.AddComponent<GameplayTagContainer>(lantern.Entity, {});
    const std::unordered_set<std::string> claimed{ lantern.Value };

    EntityReferenceFilter filter{ .Admits = [&](const EntityReferenceSubject& subject) {
        const PersistentIdComponent* id = subject.Components.TryGet<PersistentIdComponent>(subject.Entity);
        return subject.Components.HasComponent<GameplayTagContainer>(subject.Entity)
            && !claimed.contains(PersistentEntityIdToString(id->Id));
    } };
    const std::vector<EntityReferenceChoice> choices = EntityReferenceCandidates(World, filter);
    ASSERT_EQ(choices.size(), 2u);
    EXPECT_NE(Offered(choices, chest.Value), nullptr);
    EXPECT_NE(Offered(choices, lamp.Value), nullptr);
    EXPECT_EQ(Offered(choices, crate.Value), nullptr) << "an entity without the component was offered";
    EXPECT_EQ(Offered(choices, lantern.Value), nullptr) << "a claimed entity was offered";

    const std::optional<EntityReferenceSubject> admitted =
        AdmittedEntityReference(World, filter, *PersistentEntityIdFromString(lamp.Value));
    ASSERT_TRUE(admitted.has_value());
    EXPECT_EQ(&admitted->Document, &AtticDocument());
    EXPECT_EQ(admitted->Entity, lamp.Entity);
    EXPECT_FALSE(AdmittedEntityReference(World, filter, *PersistentEntityIdFromString(crate.Value)).has_value());
    EXPECT_FALSE(AdmittedEntityReference(World, filter, *PersistentEntityIdFromString(lantern.Value)).has_value());

    filter.Within = &CellarDocument();
    const std::vector<EntityReferenceChoice> withinCellar = EntityReferenceCandidates(World, filter);
    ASSERT_EQ(withinCellar.size(), 1u);
    EXPECT_EQ(withinCellar.front().Value, chest.Value);
    EXPECT_FALSE(AdmittedEntityReference(World, filter, *PersistentEntityIdFromString(lamp.Value)).has_value());
}

TEST_F(EntityReferenceTest, ATargetInAnotherLoadedZoneReadsByNameAndZoneAndIsSelectedThere)
{
    const Placed lamp = Place(AtticDocument(), "Lamp");
    std::vector<PropertyNode> nodes{ EntityReferenceProperty("target", "Target", lamp.Value, "None") };
    ShowEntityReferences(nodes, World);
    EXPECT_EQ(nodes.front().Reading, "Lamp \xC2\xB7 Attic");
    EXPECT_TRUE(OffersVerb(nodes.front(), EntityReferenceVerbs::Select));
    EXPECT_TRUE(OffersVerb(nodes.front(), EntityReferenceVerbs::Clear));

    SelectionContext context;
    SelectionService selection{ context };
    CommandStack commands;
    ASSERT_TRUE(SelectEntityReference(World, selection, commands, lamp.Value));
    EXPECT_EQ(World.FocusZone(), Attic) << "selecting a target in another zone goes to it";
    EXPECT_EQ(selection.GetPrimarySelection().Entity, lamp.Entity);
    EXPECT_EQ(selection.GetPrimarySelection().Registry, AtticDocument().GetScene().GetRegistry().Id);
}

TEST_F(EntityReferenceTest, ATargetInAZoneThatIsNotLoadedSaysSoRatherThanFailing)
{
    const std::string absent = PersistentEntityIdToString(PersistentEntityId{ 0x5AFE });

    const EntityReferenceReading unplaced = ReadEntityReference(World, absent);
    EXPECT_EQ(unplaced.State, EntityReferenceState::Unlocated);
    EXPECT_EQ(unplaced.Text, "Not in a loaded zone \xC2\xB7 0000000000005afe");

    const EntityReferenceReading expected = ReadEntityReference(World, absent, Vault);
    EXPECT_EQ(expected.State, EntityReferenceState::NotLoaded);
    EXPECT_EQ(expected.Zone, Vault);
    EXPECT_EQ(expected.Text, "In Vault, not loaded");
    PropertyNode node = EntityReferenceProperty("target", "Target", absent, "None");
    ShowEntityReference(node, expected);
    EXPECT_EQ(node.Reading, "In Vault, not loaded");
    EXPECT_FALSE(OffersVerb(node, EntityReferenceVerbs::Select)) << "a target not loaded was offered for selection";

    SelectionContext context;
    SelectionService selection{ context };
    CommandStack commands;
    EXPECT_FALSE(SelectEntityReference(World, selection, commands, absent));
    EXPECT_EQ(World.FocusZone(), Cellar);

    ASSERT_TRUE(World.LoadZone(Vault));
    const EntityReferenceReading missing = ReadEntityReference(World, absent);
    EXPECT_EQ(missing.State, EntityReferenceState::Missing) << "every document is loaded and none holds it";
    EXPECT_EQ(missing.Text, "Missing \xC2\xB7 0000000000005afe");
}

TEST_F(EntityReferenceTest, AClearedReferenceReadsAsItsFieldSaysNoneDoes)
{
    std::vector<PropertyNode> nodes{ EntityReferenceProperty("holder", "Holder", "", "The world") };
    ShowEntityReferences(nodes, World);
    EXPECT_EQ(nodes.front().Reading, "The world");
    EXPECT_TRUE(nodes.front().Descriptor.Verbs.empty()) << "nothing to clear or select";
    EXPECT_EQ(ReadEntityReference(World, "").State, EntityReferenceState::None);
    EXPECT_EQ(ReadEntityReference(World, "nowhere").State, EntityReferenceState::Malformed);
}

TEST_F(EntityReferenceTest, AResponsesCommandMayNameAnEntityAnywhereInTheWorld)
{
    const Placed lever = Place(CellarDocument(), "Lever");
    SelectionContext context;
    SelectionService selection{ context };
    CommandStack commands;
    const EditorComponentInspectorContext inspector{ .World = World,
                                                     .Document = CellarDocument(),
                                                     .Scene = CellarDocument().GetScene(),
                                                     .Selection = selection,
                                                     .Commands = commands,
                                                     .Entity = lever.Entity };
    const std::unique_ptr<IEditorComponentAdapter> responses = MakeResponsesEditorAdapter();
    const std::optional<EntityReferenceFilter> filter =
        responses->ReferenceFilter(inspector, "b.enter.s.k0.arg.target.value");
    ASSERT_TRUE(filter.has_value());
    EXPECT_EQ(filter->Within, nullptr);
    EXPECT_FALSE(responses->ReferenceFilter(inspector, "bindings").has_value());
}

// Held moves to the shared kind and stays what it was: a holder in its own
// document, never itself or what it holds, picked, cleared and undone without
// the focus moving.
TEST_F(EntityReferenceTest, HeldIsEditedThroughTheSharedKindWithoutChangingFocus)
{
    SelectionContext context;
    SelectionService selection{ context };
    CommandStack commands;
    EditorComponentAdapterRegistry adapters;
    ASSERT_TRUE(adapters.Register(MakeHeldEditorAdapter()));
    ComponentPropertySource source(World, selection, commands, adapters);

    ::World& components = CellarDocument().GetScene().GetRegistry().Components;
    const Placed chest = Place(CellarDocument(), "Chest");
    const Placed arrows = Place(CellarDocument(), "Arrows");
    const Placed lamp = Place(AtticDocument(), "Lamp");
    components.AddComponent<Held>(arrows.Entity, Held{});
    selection.BindDocument(&components);
    selection.SetSelection({ SelectableRef::EntitySelection(CellarDocument().GetScene().GetRegistry().Id,
                                                            arrows.Entity) });

    const auto holderNode = [&]() -> PropertyNode {
        for (const PropertyNode& group : source.Nodes())
            for (const PropertyNode& field : group.Children)
                if (field.Descriptor.Label == "Holder")
                    return field;
        return {};
    };
    const PropertyNode unheld = holderNode();
    ASSERT_EQ(unheld.Descriptor.Kind, PropertyKind::EntityReference);
    EXPECT_EQ(unheld.Reading, "The world");
    const std::string key = unheld.Key;

    const std::vector<EntityReferenceChoice> choices = source.ReferenceChoices(key);
    EXPECT_NE(Offered(choices, chest.Value), nullptr);
    EXPECT_EQ(Offered(choices, arrows.Value), nullptr) << "a record was offered as its own holder";
    EXPECT_EQ(Offered(choices, lamp.Value), nullptr) << "a holder is in the record's own document";

    ASSERT_TRUE(source.BeginEdit(key));
    ASSERT_EQ(source.CommitEdit(key, chest.Value), PropertyEditResult::Applied);
    EXPECT_EQ(World.FocusZone(), Cellar) << "a pick changed the focus";
    EXPECT_EQ(PersistentEntityIdToString(components.TryGet<Held>(arrows.Entity)->Holder.Target), chest.Value);
    const PropertyNode held = holderNode();
    EXPECT_EQ(held.Reading, "Chest \xC2\xB7 Cellar");
    EXPECT_TRUE(OffersVerb(held, EntityReferenceVerbs::Select));
    EXPECT_EQ(source.CommitEdit(key, lamp.Value), PropertyEditResult::Refused);

    ASSERT_EQ(source.Act(key, EntityReferenceVerbs::Clear, {}), PropertyEditResult::Applied);
    EXPECT_FALSE(components.TryGet<Held>(arrows.Entity)->Holder.Target.IsValid());
    commands.Undo();
    EXPECT_EQ(PersistentEntityIdToString(components.TryGet<Held>(arrows.Entity)->Holder.Target), chest.Value);

    ASSERT_EQ(source.Act(key, EntityReferenceVerbs::Select, {}), PropertyEditResult::Applied);
    EXPECT_EQ(selection.GetPrimarySelection().Entity, chest.Entity);
    EXPECT_EQ(World.FocusZone(), Cellar);
    commands.Undo();
    commands.Undo();
    EXPECT_FALSE(components.TryGet<Held>(arrows.Entity)->Holder.Target.IsValid());
}
