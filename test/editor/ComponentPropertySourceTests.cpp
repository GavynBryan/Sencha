#include <gtest/gtest.h>

#include "authoring/ComponentPropertySource.h"
#include "authoring/EditorComponentAdapter.h"
#include "commands/CommandStack.h"
#include "document/DocumentSerialization.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "properties/PropertyEditSession.h"
#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"

#include <core/logging/LoggingProvider.h>
#include <world/transform/TransformComponents.h>

#include <string>

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
