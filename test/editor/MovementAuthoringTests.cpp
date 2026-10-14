#include "data/DataDocumentSet.h"
#include "data/DataProperties.h"
#include "data/DataSubtypeAuthoring.h"
#include "documents/DocumentSourceSet.h"
#include "movement/MovementProfileAuthoring.h"
#include "properties/PropertyTree.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

// A movement profile read as the ruleset the runtime evaluates: each rule is
// titled and marked live or off in the situation the author dials, and beside
// the profile is what that situation resolves to and how it feels.
namespace
{
struct MovementAuthoringTest : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_movement_authoring_" + std::to_string(std::random_device{}()));
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };
    DocumentSourceSet Sources;
    DataDocumentStore Store{ Assets, Sources };
    DataSubtypeAuthoringRegistry Authoring;
    std::unique_ptr<DataDocumentSet> Set;
    std::unique_ptr<DataProperties> Properties;
    DataDocument* Document = nullptr;
    IDataSubtypeAuthoring* Movement = nullptr;

    void SetUp() override
    {
        std::filesystem::create_directories(Root / "data");
        std::ofstream(Root / "data" / "walk.sdata")
            << R"({ "type": "movement.profile", "version": 1, "data": { "name": "walk", "layers": [ )"
            << R"({ "name": "Base", "set": { "max_speed": 4.5, "friction": 6 } }, )"
            << R"({ "name": "Air control", "when": { "support": "none" }, "set": { "friction": 0 } } ] } })";
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Set = std::make_unique<DataDocumentSet>(Store, DataDocumentSetConfig{ .ContentRoot = Root, .Subtypes = {} });
        Authoring.Register(CreateMovementProfileAuthoring());
        Properties = std::make_unique<DataProperties>(*Set, [this] { return Set->Active(); }, &Authoring);
        std::string error;
        Document = Set->OpenOrFocus("asset://data/walk.sdata", error);
        ASSERT_NE(Document, nullptr) << error;
        Movement = Authoring.Find(Document->Subtype());
        ASSERT_NE(Movement, nullptr);
        Movement->Update(*Document, *Set);
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        Properties.reset();
        Set.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    [[nodiscard]] std::string Live(std::size_t layer)
    {
        std::vector<PropertyNode> nodes = Properties->Nodes();
        const std::optional<PropertyPlace> note = FindPropertyNode(nodes, std::format("$.data.layers[{}]!live", layer));
        return note.has_value() ? note->Node().Value : std::string("<none>");
    }
};
} // namespace

TEST_F(MovementAuthoringTest, ARuleIsTitledAndLiveOnlyWhereItsConditionHolds)
{
    std::vector<PropertyNode> nodes = Properties->Nodes();
    const std::optional<PropertyPlace> air = FindPropertyNode(nodes, "$.data.layers[1]");
    ASSERT_TRUE(air.has_value());
    EXPECT_EQ(air->Node().Descriptor.Label, "Air control");
    EXPECT_EQ(Live(0), "Active");
    EXPECT_TRUE(Live(1).starts_with("Off")) << "the dialled character stands on stable ground";

    const std::vector<PropertySection> sections = Movement->Sections(*Document);
    ASSERT_EQ(sections.size(), 2u);
    IPropertySource& resolve = *sections[0].Source;
    ASSERT_TRUE(resolve.BeginEdit("support"));
    EXPECT_EQ(resolve.CommitEdit("support", "In the air"), PropertyEditResult::Applied);
    EXPECT_EQ(Live(1), "Active");
    EXPECT_FALSE(Document->IsDirty()) << "dialling a situation writes nothing to the profile";
}

TEST_F(MovementAuthoringTest, BesideTheProfileIsHowItFeels)
{
    const std::vector<PropertySection> sections = Movement->Sections(*Document);
    ASSERT_EQ(sections.size(), 2u);
    std::vector<PropertyNode> response = sections[1].Source->Nodes();
    const std::optional<PropertyPlace> top = FindPropertyNode(response, "top_speed");
    ASSERT_TRUE(top.has_value());
    EXPECT_TRUE(top->Node().Value.ends_with("m/s"));
}
