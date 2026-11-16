#include "data/DataDocumentSet.h"
#include "DataSourceText.h"
#include "data/DataProperties.h"
#include "data/DataEnvelopeProperties.h"
#include "documents/DocumentSourceSet.h"

#include <anim/AnimFactSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonEquals.h>
#include <core/json/JsonParser.h>
#include <effects/EffectDefinitionData.h>
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
            << R"({ "type": "animation.fact_schema", "version": 2, "data": { "slots": [ )"
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
    EXPECT_EQ(Find("$.data.derived"), nullptr);
    const PropertyNode* root = Find("$.data");
    ASSERT_NE(root, nullptr);
    ASSERT_FALSE(root->Descriptor.Verbs.empty());
    EXPECT_EQ(Properties->Act("$.data", "add", "Derived facts"), PropertyEditResult::Applied);
    const PropertyNode* derived = Find("$.data.derived");
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->Descriptor.Kind, PropertyKind::Group);
    EXPECT_EQ(Properties->Act("$.data.derived", "remove", {}), PropertyEditResult::Applied);
    EXPECT_EQ(Find("$.data.derived"), nullptr);
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
                                R"({ "type": "animation.fact_schema", "version": 2, "data": { "slots": [] } })"),
              PropertyEditResult::Applied);
    EXPECT_EQ(SlotCount(), 0u);
    EXPECT_EQ(source.Nodes().size(), 1u);
}

// A definition that extends and includes others, laid out as it composes: each
// row says where its value came from, an edit writes only what it changes, and
// undo, redo and revert leave everything else inherited.
namespace
{
constexpr std::string_view kBase = "asset://effects/armor_base.sdata";
constexpr std::string_view kTunic = "asset://effects/tunic.sdata";

struct ComposedPropertiesTest : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_composed_properties_" + std::to_string(std::random_device{}()));
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };
    DocumentSourceSet Sources;
    DataDocumentStore Store{ Assets, Sources };
    std::unique_ptr<DataDocumentSet> Set;
    std::unique_ptr<DataProperties> Child;
    std::unique_ptr<DataProperties> Parent;
    DataDocument* Base = nullptr;
    DataDocument* Tunic = nullptr;

    void SetUp() override
    {
        std::filesystem::create_directories(Root / "effects");
        Write("armor_base.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
            "data": { "duration": "infinite", "modifiers": [
                { "key": "m_armor", "attribute": "Armor", "op": "add", "magnitude": 10 },
                { "key": "m_speed", "attribute": "MoveSpeed", "op": "add", "magnitude": 1 } ] } })");
        Write("brief.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
            "data": { "duration": "duration", "seconds": 3 } })");
        Write("lasting.sdata", R"({ "type": "effect.definition", "version": 1, "abstract": true,
            "data": { "duration": "infinite" } })");
        Write("tunic.sdata", R"({ "type": "effect.definition", "version": 1,
            "extends": "asset://effects/armor_base.sdata", "data": {} })");
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Set = std::make_unique<DataDocumentSet>(
            Store, DataDocumentSetConfig{ .ContentRoot = Root, .Subtypes = { std::string(kEffectDefinitionType) } });
        std::string error;
        Base = Set->OpenOrFocus(kBase, error);
        ASSERT_NE(Base, nullptr) << error;
        Tunic = Set->OpenOrFocus(kTunic, error);
        ASSERT_NE(Tunic, nullptr) << error;
        Child = std::make_unique<DataProperties>(*Set, [this] { return Tunic; });
        Parent = std::make_unique<DataProperties>(*Set, [this] { return Base; });
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        Child.reset();
        Parent.reset();
        Set.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    void Write(const std::string& name, std::string_view text) const
    {
        std::ofstream(Root / "effects" / name, std::ios::trunc) << text;
    }

    [[nodiscard]] const PropertyNode* Find(std::string_view key) const
    {
        Nodes = Child->Nodes();
        return DataPropertiesTest::Within(Nodes, key);
    }

    static PropertyEditResult Commit(DataProperties& properties, std::string_view key, std::string_view value)
    {
        return properties.BeginEdit(key) ? properties.CommitEdit(key, value) : PropertyEditResult::Refused;
    }

    void Step(DocumentStep step) { Store.StepDocument(Tunic->VirtualPath(), step); }

    [[nodiscard]] bool LocalIs(std::string_view json) const { return JsonEquals(*Tunic->Data(), *JsonParse(json)); }

    [[nodiscard]] const JsonValue& ComposedArmor() const
    {
        return Tunic->ComposedData().Find("modifiers")->AsArray()[0];
    }

    [[nodiscard]] static bool Offers(const PropertyNode& node, std::string_view verb)
    {
        return std::ranges::find(node.Descriptor.Verbs, verb, &PropertyVerb::Id) != node.Descriptor.Verbs.end();
    }

    mutable std::vector<PropertyNode> Nodes;
};
} // namespace

TEST_F(ComposedPropertiesTest, RowsShowTheComposedValueAndWhereItCameFrom)
{
    const PropertyNode* magnitude = Find("$.data.modifiers{m_armor}.magnitude");
    ASSERT_NE(magnitude, nullptr) << "a keyed element is addressed by its key";
    EXPECT_EQ(magnitude->Value, "10");
    EXPECT_NE(magnitude->Descriptor.Tooltip.find("inherited from asset://effects/armor_base.sdata"), std::string::npos)
        << magnitude->Descriptor.Tooltip;
    EXPECT_FALSE(Offers(*magnitude, "revert")) << "nothing is set here to revert";
    const PropertyNode* card = Find("$.data.modifiers{m_armor}");
    ASSERT_NE(card, nullptr);
    EXPECT_TRUE(Offers(*card, "remove"));
    EXPECT_TRUE(Find("$.data.modifiers{m_armor}.key")->Descriptor.ReadOnly) << "an inherited element keeps its key";
}

TEST_F(ComposedPropertiesTest, AnEditWritesOnlyTheFieldItChangesAndARevertLeavesNothingBehind)
{
    ASSERT_EQ(Commit(*Child, "$.data.modifiers{m_armor}.magnitude", "14"), PropertyEditResult::Applied);
    EXPECT_TRUE(LocalIs(R"({ "modifiers": { "m_armor": { "magnitude": 14 } } })"));
    const PropertyNode* magnitude = Find("$.data.modifiers{m_armor}.magnitude");
    ASSERT_NE(magnitude, nullptr);
    EXPECT_EQ(magnitude->Value, "14");
    EXPECT_NE(magnitude->Descriptor.Tooltip.find("set here"), std::string::npos);
    EXPECT_TRUE(Offers(*magnitude, "revert"));

    Step(DocumentStep::Undo);
    EXPECT_TRUE(LocalIs("{}"));
    Step(DocumentStep::Redo);
    EXPECT_TRUE(LocalIs(R"({ "modifiers": { "m_armor": { "magnitude": 14 } } })"));

    EXPECT_EQ(Child->Act("$.data.modifiers{m_armor}.magnitude", "revert", {}), PropertyEditResult::Applied);
    EXPECT_TRUE(LocalIs("{}")) << "the emptied element entry and list were erased too";
    EXPECT_EQ(ComposedArmor().Find("magnitude")->AsNumber(), 10.0);
}

TEST_F(ComposedPropertiesTest, AnOverriddenFieldKeepsInheritingItsSiblings)
{
    ASSERT_EQ(Commit(*Child, "$.data.modifiers{m_armor}.magnitude", "14"), PropertyEditResult::Applied);
    ASSERT_EQ(Commit(*Parent, "$.data.modifiers[0].attribute", "Ward"), PropertyEditResult::Applied);
    EXPECT_EQ(ComposedArmor().Find("attribute")->AsString(), "Ward") << "the base's change to a sibling reached the child";
    EXPECT_EQ(ComposedArmor().Find("magnitude")->AsNumber(), 14.0) << "the child's override held";

    Step(DocumentStep::Undo);
    EXPECT_EQ(ComposedArmor().Find("attribute")->AsString(), "Ward");
    EXPECT_EQ(ComposedArmor().Find("magnitude")->AsNumber(), 10.0);
    Step(DocumentStep::Redo);
    EXPECT_EQ(ComposedArmor().Find("attribute")->AsString(), "Ward");
    EXPECT_EQ(ComposedArmor().Find("magnitude")->AsNumber(), 14.0);

    ASSERT_EQ(Child->Act("$.data.modifiers{m_armor}.magnitude", "revert", {}), PropertyEditResult::Applied);
    ASSERT_EQ(Commit(*Parent, "$.data.modifiers[0].magnitude", "12"), PropertyEditResult::Applied);
    EXPECT_EQ(ComposedArmor().Find("magnitude")->AsNumber(), 12.0) << "a revert restores full inheritance";
}

TEST_F(ComposedPropertiesTest, AnInheritedElementIsRemovedHereAndRestored)
{
    ASSERT_EQ(Child->Act("$.data.modifiers{m_speed}", "remove", {}), PropertyEditResult::Applied);
    EXPECT_TRUE(LocalIs(R"({ "modifiers": { "m_speed": null } })"));
    const PropertyNode* removed = Find("$.data.modifiers{m_speed}");
    ASSERT_NE(removed, nullptr) << "a removed element stays in view";
    EXPECT_TRUE(removed->Descriptor.ReadOnly);
    EXPECT_EQ(removed->Descriptor.Tooltip, "removed here");
    EXPECT_TRUE(Offers(*removed, "restore"));

    ASSERT_EQ(Child->Act("$.data.modifiers{m_speed}", "restore", {}), PropertyEditResult::Applied);
    EXPECT_TRUE(LocalIs("{}"));
}

TEST_F(ComposedPropertiesTest, AnElementAddedHereIsWrittenByKeyAndCanBeRenamed)
{
    ASSERT_EQ(Child->Act("$.data.modifiers", "add", {}), PropertyEditResult::Applied);
    const JsonValue* added = Tunic->Data()->Find("modifiers");
    ASSERT_TRUE(added != nullptr && added->IsObject() && added->AsObject().size() == 1);
    const std::string key = added->AsObject()[0].first;
    EXPECT_EQ(added->AsObject()[0].second.Find("key")->AsString(), key);

    const std::string keyPath = "$.data.modifiers{" + key + "}.key";
    ASSERT_FALSE(Find(keyPath)->Descriptor.ReadOnly);
    ASSERT_EQ(Commit(*Child, keyPath, "m_fire"), PropertyEditResult::Applied);
    ASSERT_NE(Tunic->Data()->Find("modifiers")->Find("m_fire"), nullptr);
    EXPECT_EQ(FindValidationErrorAt(Tunic->ValidationErrors(), "$.data.modifiers{m_fire}.key"), nullptr)
        << "the element composes under its new key";
    EXPECT_EQ(Commit(*Child, "$.data.modifiers{m_fire}.key", "m_armor"), PropertyEditResult::Refused)
        << "another element already has that key";
}

TEST_F(ComposedPropertiesTest, AConflictBetweenFragmentsIsSettledByTakingOnesValue)
{
    JsonValue root = Tunic->CopyRoot();
    root.AsObject().emplace_back("includes", *JsonParse(R"([ "asset://effects/brief.sdata", "asset://effects/lasting.sdata" ])"));
    Set->ApplyEdit(*Tunic, std::move(root), DataEdit::Step);
    ASSERT_FALSE(Tunic->IsSemanticallyValid());
    const PropertyNode* duration = Find("$.data.duration");
    ASSERT_NE(duration, nullptr);
    ASSERT_TRUE(Offers(*duration, "take"));

    ASSERT_EQ(Child->Act("$.data.duration", "take", "asset://effects/lasting.sdata"), PropertyEditResult::Applied);
    EXPECT_TRUE(LocalIs(R"({ "duration": "infinite" })"));
    EXPECT_TRUE(Tunic->IsSemanticallyValid()) << Tunic->ValidationErrors().front().Message;
}

TEST_F(ComposedPropertiesTest, TheEnvelopeRowsChooseWhatADefinitionExtendsAndIncludes)
{
    DataEnvelopeProperties envelope(*Set, [this] { return Tunic; });
    const std::vector<PropertyNode> rows = envelope.Nodes();
    const PropertyNode* extends = DataPropertiesTest::Within(rows, "$.extends");
    ASSERT_NE(extends, nullptr);
    EXPECT_EQ(extends->Value, kBase);
    const std::vector<std::string> candidates = envelope.Candidates("$.extends");
    EXPECT_NE(std::ranges::find(candidates, std::string(kBase)), candidates.end()) << "abstract bases are offered";
    EXPECT_EQ(std::ranges::find(candidates, std::string(kTunic)), candidates.end()) << "never itself";

    ASSERT_EQ(envelope.Act("$.includes", "add", "asset://effects/brief.sdata"), PropertyEditResult::Applied);
    EXPECT_EQ(Tunic->ComposedData().Find("duration")->AsString(), "duration") << "the fragment applies over the base";
    ASSERT_EQ(envelope.Act("$.includes[0]", "delete", {}), PropertyEditResult::Applied);
    EXPECT_EQ(Tunic->Root().Find("includes"), nullptr) << "an empty list is not left behind";

    ASSERT_TRUE(envelope.BeginEdit("$.abstract"));
    ASSERT_EQ(envelope.CommitEdit("$.abstract", "true"), PropertyEditResult::Applied);
    EXPECT_TRUE(Tunic->Root().Find("abstract")->AsBool());
    ASSERT_TRUE(envelope.BeginEdit("$.abstract"));
    ASSERT_EQ(envelope.CommitEdit("$.abstract", "false"), PropertyEditResult::Applied);
    EXPECT_EQ(Tunic->Root().Find("abstract"), nullptr);

    ASSERT_TRUE(envelope.BeginEdit("$.extends"));
    ASSERT_EQ(envelope.CommitEdit("$.extends", ""), PropertyEditResult::Applied);
    EXPECT_FALSE(Tunic->Composes()) << "with nothing to extend it stands alone";
}
