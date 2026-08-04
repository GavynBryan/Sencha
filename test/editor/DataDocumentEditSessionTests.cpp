#include <gtest/gtest.h>

#include "DataDocument.h"

#include <movement/MovementProfileData.h>

#include <core/json/JsonFormat.h>

#include <memory>
#include <string>

//=============================================================================
// A drag reports a change every frame it moves. Without a transaction each of
// those frames became its own full-document undo entry, so undo after tuning a
// value walked back through a hundred intermediate positions. These cover the
// scope: one interaction, one entry, and every interruption path terminating it.
//=============================================================================

namespace
{
    struct Registered
    {
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;

        Registered() { RegisterMovementProfileData(Types, Schemas); }
    };

    std::unique_ptr<DataDocument> MakeDocument(Registered& registered)
    {
        const DataAssetTypeRegistration* type = registered.Types.Find("movement.profile");
        const DataSchema* schema = registered.Schemas.Find("movement.profile");
        if (type == nullptr || schema == nullptr)
            return nullptr;
        return DataDocument::Create("movement_profile_test.sdata",
                                    "asset://data/movement_profile_test.sdata",
                                    *type, *schema);
    }

    // Stands in for one frame of a drag: the form copies the root, writes the
    // new value into the copy, and hands it back.
    JsonValue WithName(const DataDocument& document, std::string name)
    {
        JsonValue root = document.CopyRoot();
        JsonValue* data = root.Find("data");
        if (data != nullptr && data->IsObject())
        {
            if (JsonValue* field = data->Find("name"))
                *field = JsonValue(std::move(name));
        }
        return root;
    }

    std::string NameOf(const DataDocument& document)
    {
        const JsonValue* data = document.Data();
        if (data == nullptr)
            return {};
        const JsonValue* field = data->Find("name");
        return field && field->IsString() ? field->AsString() : std::string{};
    }
}

// An optional member's default is the value it starts at once the author adds
// it. Materializing every optional member with a default would hand back a
// record full of values nobody asked for -- with eight optional coefficients per
// operation, that buries the ones actually set.
TEST(DataDocumentEditSession, OptionalMembersAreNotMaterializedByTheirDefault)
{
    Registered registered;
    const DataSchema* schema = registered.Schemas.Find("movement.profile");
    ASSERT_NE(schema, nullptr);

    const DataFieldSchema* layers = FindChild(schema->Root, "layers");
    ASSERT_NE(layers, nullptr);
    ASSERT_FALSE(layers->Children.empty());

    const JsonValue layer = CreateDefaultDataValue(layers->Children.front());
    ASSERT_TRUE(layer.IsObject());
    EXPECT_EQ(layer.Find("set"), nullptr) << "an optional patch record stays absent";
    EXPECT_EQ(layer.Find("when"), nullptr);
    EXPECT_EQ(layer.Find("name"), nullptr);

    // Added explicitly, it starts at the operation's neutral value.
    const DataFieldSchema* scale = FindChild(layers->Children.front(), "scale");
    ASSERT_NE(scale, nullptr);
    const DataFieldSchema* scaledFriction = FindChild(*scale, "friction");
    ASSERT_NE(scaledFriction, nullptr);
    const JsonValue added = CreateDefaultDataValue(*scaledFriction);
    ASSERT_TRUE(added.IsNumber());
    EXPECT_DOUBLE_EQ(added.AsNumber(), 1.0);
}

TEST(DataDocumentEditSession, OneInteractionLeavesOneUndoEntry)
{
    Registered registered;
    const auto document = MakeDocument(registered);
    ASSERT_NE(document, nullptr);
    const std::string original = NameOf(*document);

    document->BeginEdit();
    for (int frame = 0; frame < 20; ++frame)
        document->PreviewRoot(WithName(*document, "step" + std::to_string(frame)));
    document->CommitEdit();

    EXPECT_EQ(NameOf(*document), "step19");
    ASSERT_TRUE(document->CanUndo());

    document->Undo();
    EXPECT_EQ(NameOf(*document), original) << "one undo must reach the pre-drag value";
    EXPECT_FALSE(document->CanUndo());
}

TEST(DataDocumentEditSession, CancelRestoresThePreInteractionValue)
{
    Registered registered;
    const auto document = MakeDocument(registered);
    ASSERT_NE(document, nullptr);
    const std::string original = NameOf(*document);

    document->BeginEdit();
    document->PreviewRoot(WithName(*document, "abandoned"));
    EXPECT_EQ(NameOf(*document), "abandoned");

    document->CancelEdit();
    EXPECT_EQ(NameOf(*document), original);
    EXPECT_FALSE(document->CanUndo()) << "a cancelled interaction leaves no history";
    EXPECT_FALSE(document->IsEditing());
}

TEST(DataDocumentEditSession, UndoDuringAnInteractionCancelsItFirst)
{
    Registered registered;
    const auto document = MakeDocument(registered);
    ASSERT_NE(document, nullptr);

    document->BeginEdit();
    document->PreviewRoot(WithName(*document, "committed"));
    document->CommitEdit();

    const std::string committed = NameOf(*document);
    ASSERT_EQ(committed, "committed");

    document->BeginEdit();
    document->PreviewRoot(WithName(*document, "in flight"));

    // The first undo reverts the live interaction, not the entry behind it.
    document->Undo();
    EXPECT_EQ(NameOf(*document), committed);
    EXPECT_FALSE(document->IsEditing());

    document->Undo();
    EXPECT_NE(NameOf(*document), committed);
}

TEST(DataDocumentEditSession, AnInteractionThatChangesNothingLeavesNoHistory)
{
    Registered registered;
    const auto document = MakeDocument(registered);
    ASSERT_NE(document, nullptr);

    document->BeginEdit();
    document->PreviewRoot(document->CopyRoot());
    document->CommitEdit();

    EXPECT_FALSE(document->CanUndo());
}

TEST(DataDocumentEditSession, CommitAndCancelAreIdempotentOutsideAnInteraction)
{
    Registered registered;
    const auto document = MakeDocument(registered);
    ASSERT_NE(document, nullptr);
    const std::string original = NameOf(*document);

    document->CommitEdit();
    document->CancelEdit();
    EXPECT_EQ(NameOf(*document), original);
    EXPECT_FALSE(document->CanUndo());
}

TEST(DataDocumentEditSession, RevisionAdvancesOnPreviewsSoDerivedViewsRefresh)
{
    Registered registered;
    const auto document = MakeDocument(registered);
    ASSERT_NE(document, nullptr);

    const uint64_t start = document->Revision();
    document->BeginEdit();
    document->PreviewRoot(WithName(*document, "first"));
    const uint64_t afterPreview = document->Revision();
    EXPECT_GT(afterPreview, start);

    document->PreviewRoot(WithName(*document, "second"));
    EXPECT_GT(document->Revision(), afterPreview);

    document->CommitEdit();
    document->Undo();
    EXPECT_GT(document->Revision(), afterPreview) << "undo is also a content change";
}
