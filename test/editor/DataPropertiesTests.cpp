#include "data/DataDocumentSet.h"
#include "DataSourceText.h"
#include "data/DataProperties.h"
#include "documents/DocumentSourceSet.h"

#include <anim/AnimFactSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

// A data document laid out by its schema as properties keyed by JSON path: a
// typed value is one step, an array's verbs are steps, an absent optional
// member is added from its record, a drag previews until it lets go, and a
// cancelled drag leaves the document as it was.
namespace
{
constexpr std::string_view kFacts = "asset://animation/game.facts.sdata";

struct DataPropertiesTest : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_data_properties_" + std::to_string(std::random_device{}()));
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };
    DocumentSourceSet Sources;
    DataDocumentStore Store{ Assets, Sources };
    std::unique_ptr<DataDocumentSet> Set;
    std::unique_ptr<DataProperties> Properties;
    DataDocument* Document = nullptr;

    void SetUp() override
    {
        std::filesystem::create_directories(Root / "animation");
        std::ofstream(Root / "animation" / "game.facts.sdata")
            << R"({ "type": "animation.fact_schema", "version": 1, "data": { "slots": [ )"
            << R"({ "name": "Crouched", "kind": "bool" }, { "name": "Speed", "kind": "float" } ] } })";
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Set = std::make_unique<DataDocumentSet>(
            Store, DataDocumentSetConfig{ .ContentRoot = Root, .Subtypes = { std::string(kAnimFactSchemaType) } });
        std::string error;
        Document = Set->OpenOrFocus(kFacts, error);
        ASSERT_NE(Document, nullptr) << error;
        Properties = std::make_unique<DataProperties>(*Set, [this] { return Set->Active(); });
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        Properties.reset();
        Set.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    [[nodiscard]] const PropertyNode* Find(std::string_view key) const
    {
        Nodes = Properties->Nodes();
        return Within(Nodes, key);
    }

    static const PropertyNode* Within(const std::vector<PropertyNode>& nodes, std::string_view key)
    {
        for (const PropertyNode& node : nodes)
        {
            if (node.Key == key)
                return &node;
            if (const PropertyNode* child = Within(node.Children, key))
                return child;
        }
        return nullptr;
    }

    PropertyEditResult Commit(std::string_view key, std::string_view value)
    {
        return Properties->BeginEdit(key) ? Properties->CommitEdit(key, value) : PropertyEditResult::Refused;
    }

    [[nodiscard]] std::size_t SlotCount() const { return Document->Data()->Find("slots")->AsArray().size(); }

    mutable std::vector<PropertyNode> Nodes;
};
} // namespace

TEST_F(DataPropertiesTest, FieldsAreKeyedByTheirPathAndElementsTitledByTheirName)
{
    const PropertyNode* slots = Find("$.data.slots");
    ASSERT_NE(slots, nullptr);
    ASSERT_EQ(slots->Children.size(), 2u);
    EXPECT_EQ(slots->Children[0].Descriptor.Label, "Crouched") << "the title key names the card";
    const PropertyNode* kind = Find("$.data.slots[1].kind");
    ASSERT_NE(kind, nullptr);
    EXPECT_EQ(kind->Descriptor.Kind, PropertyKind::Choice);
    EXPECT_EQ(kind->Value, "Float") << "an enum shows its display name";
}

TEST_F(DataPropertiesTest, ATypedValueIsOneStep)
{
    EXPECT_EQ(Commit("$.data.slots[0].name", "Standing"), PropertyEditResult::Applied);
    EXPECT_EQ(Document->Data()->Find("slots")->AsArray()[0].Find("name")->AsString(), "Standing");
    EXPECT_EQ(Commit("$.data.slots[1].kind", "Integer"), PropertyEditResult::Applied);
    EXPECT_EQ(Document->Data()->Find("slots")->AsArray()[1].Find("kind")->AsString(), "int");
    EXPECT_EQ(Commit("$.data.slots[1].kind", "Nonsense"), PropertyEditResult::Refused);
    Document->Undo();
    EXPECT_EQ(Document->Data()->Find("slots")->AsArray()[1].Find("kind")->AsString(), "float");
    EXPECT_FALSE(Document->IsEditing()) << "a refused value leaves no edit open";
}

TEST_F(DataPropertiesTest, AnArraysVerbsAreSteps)
{
    EXPECT_EQ(Properties->Act("$.data.slots", "add", {}), PropertyEditResult::Applied);
    EXPECT_EQ(SlotCount(), 3u);
    EXPECT_EQ(Properties->Act("$.data.slots[0]", "down", {}), PropertyEditResult::Applied);
    EXPECT_EQ(Document->Data()->Find("slots")->AsArray()[1].Find("name")->AsString(), "Crouched");
    EXPECT_EQ(Properties->Act("$.data.slots[1]", "delete", {}), PropertyEditResult::Applied);
    EXPECT_EQ(SlotCount(), 2u);
    Document->Undo();
    EXPECT_EQ(SlotCount(), 3u);
}

TEST_F(DataPropertiesTest, AnAbsentOptionalMemberIsAddedFromItsRecord)
{
    EXPECT_EQ(Find("$.data.extends"), nullptr);
    const PropertyNode* root = Find("$.data");
    ASSERT_NE(root, nullptr);
    ASSERT_FALSE(root->Descriptor.Verbs.empty());
    EXPECT_EQ(Properties->Act("$.data", "add", "Extends"), PropertyEditResult::Applied);
    const PropertyNode* extends = Find("$.data.extends");
    ASSERT_NE(extends, nullptr);
    EXPECT_EQ(extends->Descriptor.Kind, PropertyKind::Asset);
    EXPECT_EQ(Properties->Act("$.data.extends", "remove", {}), PropertyEditResult::Applied);
    EXPECT_EQ(Find("$.data.extends"), nullptr);
}

TEST_F(DataPropertiesTest, ACancelledDragLeavesTheDocumentAsItWas)
{
    ASSERT_TRUE(Properties->BeginEdit("$.data.slots[0].name"));
    EXPECT_EQ(Properties->PreviewEdit("$.data.slots[0].name", "Prone"), PropertyEditResult::Applied);
    EXPECT_TRUE(Document->IsEditing());
    Properties->CancelEdit("$.data.slots[0].name");
    EXPECT_FALSE(Document->IsEditing());
    EXPECT_EQ(Document->Data()->Find("slots")->AsArray()[0].Find("name")->AsString(), "Crouched");
    EXPECT_FALSE(Document->IsDirty());
}

TEST_F(DataPropertiesTest, TheSourceReplacesTheDocumentWhenItParsesAndSaysWhereWhenItDoesNot)
{
    DataSourceText source(*Set, [this] { return Set->Active(); });
    std::vector<PropertyNode> nodes = source.Nodes();
    ASSERT_FALSE(nodes.empty());
    EXPECT_TRUE(nodes[0].Descriptor.Multiline);
    ASSERT_TRUE(source.BeginEdit("source"));
    EXPECT_EQ(source.CommitEdit("source", "{ not json"), PropertyEditResult::Refused);
    nodes = source.Nodes();
    ASSERT_EQ(nodes.size(), 2u) << "the problem is said beside the source";
    ASSERT_TRUE(source.BeginEdit("source"));
    EXPECT_EQ(source.CommitEdit("source",
                                R"({ "type": "animation.fact_schema", "version": 1, "data": { "slots": [] } })"),
              PropertyEditResult::Applied);
    EXPECT_EQ(SlotCount(), 0u);
    EXPECT_EQ(source.Nodes().size(), 1u);
}
