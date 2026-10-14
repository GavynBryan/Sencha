#include <gtest/gtest.h>

#include "authoring/ComponentPropertySource.h"
#include "authoring/EditorComponentAdapter.h"
#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EntityNameComponent.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "properties/PropertyEditSession.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <vector>

// The selected entity's components as properties, and the one way they change.
// What matters is the transaction: a live edit writes the component at once but
// is not a step until it commits, and every way of abandoning it -- cancelling,
// undoing, beginning another -- puts the bytes back.

namespace
{
class Fixture
{
public:
    Fixture()
        : World(Logging)
        , Selection(SelectionCtx)
        , Source(World, Selection, Commands, Adapters)
    {
        Selection.BindDocument(&Components());
        Entity = World.FocusDocument().GetScene().CreateEntity(Vec3d(1.0f, 2.0f, 3.0f));
        Selection.SetSelection({ SelectableRef::EntitySelection(
            World.FocusDocument().GetScene().GetRegistry().Id, Entity) });
    }

    [[nodiscard]] ::World& Components() { return World.FocusDocument().GetScene().GetRegistry().Components; }
    [[nodiscard]] Vec3d Position() { return Components().TryGet<LocalTransform>(Entity)->Value.Position; }

    [[nodiscard]] std::string PositionKey() const
    {
        for (const PropertyNode& group : Source.Nodes())
            for (const PropertyNode& field : group.Children)
                if (field.Descriptor.Label == "Position")
                    return field.Key;
        return {};
    }

    struct SerializersFirst
    {
        SerializersFirst() { RegisterDocumentSerializers(); }
    } Registered;

    LoggingProvider Logging;
    WorldDocument World;
    SelectionContext SelectionCtx;
    SelectionService Selection;
    CommandStack Commands;
    EditorComponentAdapterRegistry Adapters;
    ComponentPropertySource Source;
    EntityId Entity{};
};
} // namespace

TEST(ComponentPropertySource, AComponentIsAGroupAndItsFieldsAreKeyedProperties)
{
    Fixture fixture;
    const std::string key = fixture.PositionKey();
    ASSERT_FALSE(key.empty()) << "the transform's position was not presented";
    EXPECT_NE(key.find('/'), std::string::npos) << "a field's key does not name its component";
    for (const PropertyNode& group : fixture.Source.Nodes())
    {
        EXPECT_EQ(group.Descriptor.Kind, PropertyKind::Group);
        for (const PropertyNode& field : group.Children)
            if (field.Key == key)
            {
                EXPECT_EQ(field.Descriptor.Kind, PropertyKind::Vector);
                EXPECT_EQ(field.Value, "1, 2, 3");
            }
    }
}

TEST(ComponentPropertySource, APreviewShowsAtOnceAndIsNotAStepUntilItCommits)
{
    Fixture fixture;
    const std::string key = fixture.PositionKey();
    PropertyEditSession session;
    ASSERT_TRUE(session.Begin(fixture.Source, key));

    EXPECT_EQ(session.Preview("4, 5, 6"), PropertyEditResult::Applied);
    EXPECT_FLOAT_EQ(fixture.Position().X, 4.0f) << "a preview did not reach the component";
    EXPECT_TRUE(fixture.Commands.HasPendingEdit()) << "a live write outside the pending-edit scope";

    EXPECT_EQ(session.Commit("7, 8, 9"), PropertyEditResult::Applied);
    EXPECT_FALSE(fixture.Commands.HasPendingEdit());
    EXPECT_FLOAT_EQ(fixture.Position().X, 7.0f);

    fixture.Commands.Undo();
    EXPECT_FLOAT_EQ(fixture.Position().X, 1.0f) << "the whole drag was not one step";
}

TEST(ComponentPropertySource, EveryWayOfAbandoningAnEditPutsTheBytesBack)
{
    Fixture fixture;
    const std::string key = fixture.PositionKey();
    PropertyEditSession session;

    ASSERT_TRUE(session.Begin(fixture.Source, key));
    (void)session.Preview("4, 5, 6");
    session.Cancel();
    EXPECT_FLOAT_EQ(fixture.Position().X, 1.0f) << "cancelling kept the preview";
    EXPECT_FALSE(fixture.Commands.CanUndo());

    ASSERT_TRUE(session.Begin(fixture.Source, key));
    (void)session.Preview("4, 5, 6");
    fixture.Commands.Undo();
    EXPECT_FLOAT_EQ(fixture.Position().X, 1.0f) << "undo mid-preview kept the preview";

    ASSERT_TRUE(session.Begin(fixture.Source, key));
    (void)session.Preview("4, 5, 6");
    fixture.Commands.OpenPendingEdit([] {});
    EXPECT_FLOAT_EQ(fixture.Position().X, 1.0f) << "another staged edit beginning kept this one's preview";
    fixture.Commands.ClosePendingEdit();
}

TEST(ComponentPropertySource, AValueTheFieldCannotHoldIsRefusedAndChangesNothing)
{
    Fixture fixture;
    PropertyEditSession session;
    EXPECT_EQ(session.Set(fixture.Source, fixture.PositionKey(), "4, five, 6"), PropertyEditResult::Refused);
    EXPECT_FLOAT_EQ(fixture.Position().X, 1.0f);
    EXPECT_FALSE(fixture.Commands.CanUndo());
    EXPECT_EQ(session.Set(fixture.Source, fixture.PositionKey(), "1, 2, 3"), PropertyEditResult::Unchanged);
    EXPECT_FALSE(fixture.Commands.CanUndo()) << "setting what is already there made a step";
    EXPECT_EQ(session.Set(fixture.Source, "missing/field", "1"), PropertyEditResult::Gone);
}

// What was the ImGui inspector's is the source's now: components added and
// removed as steps, an adapter's own properties routed to it, asset fields
// chosen from what the project holds.
namespace
{
// Speaks for the name component: one property of its own and one verb, so
// routing to an adapter is visible without a real one's registries.
class NameAdapter final : public IEditorComponentAdapter
{
public:
    [[nodiscard]] ComponentTypeId Type() const override { return ResolveComponentTypeId<EntityNameComponent>(); }
    [[nodiscard]] bool AuthorsInspectorRows() const override { return true; }
    std::vector<PropertyNode> InspectorNodes(const EditorComponentInspectorContext&) const override
    {
        PropertyNode shout{ .Key = "shout", .Descriptor = { .Kind = PropertyKind::Text, .Label = "Shout" } };
        shout.Descriptor.Verbs.push_back({ .Id = "clear", .Label = "Clear" });
        return { std::move(shout) };
    }
    PropertyEditResult SetInspectorValue(EditorComponentInspectorContext&, std::string_view key,
                                         std::string_view value) const override
    {
        Log.push_back("set " + std::string(key) + "=" + std::string(value));
        return PropertyEditResult::Applied;
    }
    PropertyEditResult ActInspector(EditorComponentInspectorContext&, std::string_view key, std::string_view verb,
                                    std::string_view) const override
    {
        Log.push_back(std::string(verb) + " " + std::string(key));
        return PropertyEditResult::Applied;
    }
    mutable std::vector<std::string> Log;
};

const PropertyNode* FindNode(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* child = FindNode(node.Children, key))
            return child;
    }
    return nullptr;
}
} // namespace

TEST(ComponentPropertySource, RemovingAndAddingAComponentAreStepsLikeAnyOther)
{
    Fixture f;
    const std::vector<PropertyNode> nodes = f.Source.Nodes();
    const PropertyNode* name = FindNode(nodes, "name");
    ASSERT_NE(name, nullptr) << "the scene stamps a name on what it creates";
    ASSERT_TRUE(std::ranges::any_of(name->Descriptor.Verbs, [](const PropertyVerb& v) { return v.Id == "remove"; }));

    EXPECT_EQ(f.Source.Act("name", "remove", {}), PropertyEditResult::Applied);
    EXPECT_EQ(FindNode(f.Source.Nodes(), "name"), nullptr);

    const std::vector<PropertyNode> afterRemoval = f.Source.Nodes();
    const PropertyNode* components = FindNode(afterRemoval, "components");
    ASSERT_NE(components, nullptr);
    ASSERT_FALSE(components->Descriptor.Verbs.empty());
    const PropertyVerb& add = components->Descriptor.Verbs.front();
    const auto offered = std::ranges::find(add.ChoiceKeys, std::string("name"));
    ASSERT_NE(offered, add.ChoiceKeys.end()) << "what was removed can come back";
    EXPECT_EQ(add.Choices[static_cast<std::size_t>(offered - add.ChoiceKeys.begin())], name->Descriptor.Label)
        << "offered by the name it reads as, chosen by its key";
    EXPECT_EQ(f.Source.Act("components", "add", "name"), PropertyEditResult::Applied);
    EXPECT_NE(FindNode(f.Source.Nodes(), "name"), nullptr);

    f.Commands.Undo();
    f.Commands.Undo();
    EXPECT_NE(FindNode(f.Source.Nodes(), "name"), nullptr) << "undoing both puts the original back";
}

TEST(ComponentPropertySource, AnAdaptersPropertiesAndVerbsReachTheAdapter)
{
    Fixture f;
    auto adapter = std::make_unique<NameAdapter>();
    NameAdapter* spoken = adapter.get();
    ASSERT_TRUE(f.Adapters.Register(std::move(adapter)));

    EXPECT_NE(FindNode(f.Source.Nodes(), "name:shout"), nullptr) << "the adapter's property, keyed under its component";
    ASSERT_TRUE(f.Source.BeginEdit("name:shout"));
    EXPECT_EQ(f.Source.CommitEdit("name:shout", "hey"), PropertyEditResult::Applied);
    EXPECT_EQ(f.Source.Act("name:shout", "clear", {}), PropertyEditResult::Applied);
    EXPECT_EQ(spoken->Log, (std::vector<std::string>{ "set shout=hey", "clear shout" }));
}

TEST(ComponentPropertySource, AnEntitysNameIsEditedAndOneTooLongIsRefused)
{
    Fixture f;
    const std::vector<PropertyNode> nodes = f.Source.Nodes();
    const PropertyNode* name = FindNode(nodes, "name");
    ASSERT_NE(name, nullptr);
    ASSERT_FALSE(name->Children.empty());
    const std::string key = name->Children.front().Key;

    ASSERT_TRUE(f.Source.BeginEdit(key));
    EXPECT_EQ(f.Source.CommitEdit(key, "Crate"), PropertyEditResult::Applied);
    EXPECT_EQ(FindNode(f.Source.Nodes(), key)->Value, "Crate");

    ASSERT_TRUE(f.Source.BeginEdit(key));
    EXPECT_EQ(f.Source.CommitEdit(key, std::string(200, 'x')), PropertyEditResult::Refused);
    EXPECT_EQ(FindNode(f.Source.Nodes(), key)->Value, "Crate");
}

// The sheet's order is the order of attention: what can be added first, the
// components, and last -- closed, grouped by what each came from -- what the
// engine put on the entity because of them. A component's own actions travel
// with it, and taking one away is marked as such.
namespace
{
class ActingNameAdapter final : public IEditorComponentAdapter
{
public:
    [[nodiscard]] ComponentTypeId Type() const override { return ResolveComponentTypeId<EntityNameComponent>(); }
    [[nodiscard]] std::span<const std::string_view> Actions() const override
    {
        static constexpr std::array<std::string_view, 1> kActions = { "name.shout" };
        return kActions;
    }
};
} // namespace

TEST(ComponentPropertySource, AddingComesFirstAndDerivedComponentsLastClosedUnderTheirSource)
{
    Fixture f;
    ASSERT_EQ(f.Source.Act("components", "add", "CharacterMovement"), PropertyEditResult::Applied);
    const std::vector<PropertyNode> nodes = f.Source.Nodes();
    ASSERT_FALSE(nodes.empty());
    EXPECT_EQ(nodes.front().Key, "components");

    const PropertyNode& derived = nodes.back();
    ASSERT_EQ(derived.Key, "derived");
    EXPECT_TRUE(derived.Descriptor.Folded);
    EXPECT_FALSE(derived.Descriptor.Advanced) << "always there to read";
    ASSERT_FALSE(derived.Children.empty());
    const PropertyNode& from = derived.Children.front();
    EXPECT_EQ(from.Descriptor.Kind, PropertyKind::Group);
    EXPECT_EQ(from.Descriptor.Label, "from Character Movement");
    EXPECT_GT(from.Children.size(), 5u) << "what character movement owes";
}

TEST(ComponentPropertySource, RemovalIsMarkedAndAComponentsActionsTravelWithIt)
{
    Fixture f;
    ASSERT_TRUE(f.Adapters.Register(std::make_unique<ActingNameAdapter>()));
    const std::vector<PropertyNode> nodes = f.Source.Nodes();
    const PropertyNode* name = FindNode(nodes, "name");
    ASSERT_NE(name, nullptr);
    const auto remove = std::ranges::find(name->Descriptor.Verbs, "remove", &PropertyVerb::Id);
    ASSERT_NE(remove, name->Descriptor.Verbs.end());
    EXPECT_TRUE(remove->Removes);
    EXPECT_EQ(name->Descriptor.Actions, (std::vector<std::string>{ "name.shout" }));
    EXPECT_TRUE(name->Descriptor.Folded) << "a component starts closed, its actions and fields inside it";
}

TEST(ComponentPropertySource, AComponentOnOfferSaysWhatAddingItBringsWithIt)
{
    Fixture f;
    const std::vector<PropertyNode> nodes = f.Source.Nodes();
    const PropertyVerb& add = nodes.front().Descriptor.Verbs.front();
    ASSERT_EQ(add.ChoiceNotes.size(), add.Choices.size());
    const auto movement = std::ranges::find(add.Choices, "Character Movement");
    ASSERT_NE(movement, add.Choices.end());
    const std::string& note = add.ChoiceNotes[static_cast<std::size_t>(movement - add.Choices.begin())];
    EXPECT_TRUE(note.starts_with("Also adds ")) << note;
    EXPECT_NE(note.find("Jump State"), std::string::npos) << note;
}
