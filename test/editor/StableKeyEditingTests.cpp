#include "data/DataDocumentSet.h"
#include "data/DataProperties.h"
#include "documents/DocumentSourceSet.h"
#include "identity/RandomKey.h"

#include <assets/runtime/RuntimeAssets.h>
#include <attributes/AttributeDeclarations.h>
#include <core/assets/AssetRegistry.h>
#include <core/metadata/DataSchema.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

// An element's stable key is minted when the element is made, and a copy gets a
// key of its own: duplicating never makes two elements share an identity.
namespace
{
constexpr std::string_view kDeclarations = "asset://gameplay/game.attributes.sdata";

struct StableKeyEditingTest : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
        / ("sencha_stable_keys_" + std::to_string(std::random_device{}()));
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
        std::filesystem::create_directories(Root / "gameplay");
        std::ofstream(Root / "gameplay" / "game.attributes.sdata")
            << R"({ "type": "gameplay.attribute_declarations", "version": 1, "data": { "attributes": [ )"
            << R"({ "key": "ammo", "name": "Ammo", "min": 0, "max": 30, "default": 30 } ] } })";
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Set = std::make_unique<DataDocumentSet>(
            Store, DataDocumentSetConfig{ .ContentRoot = Root, .Subtypes = { std::string(kAttributeDeclarationsType) } });
        std::string error;
        Document = Set->OpenOrFocus(kDeclarations, error);
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

    [[nodiscard]] std::string KeyAt(std::size_t index) const
    {
        return Document->Data()->Find("attributes")->AsArray()[index].Find("key")->AsString();
    }
};
}

TEST_F(StableKeyEditingTest, AnAddedElementGetsAFreshKey)
{
    ASSERT_EQ(Properties->Act("$.data.attributes", "add", {}), PropertyEditResult::Applied);
    const std::string added = KeyAt(1);
    EXPECT_TRUE(IsValidStableKey(added));
    EXPECT_EQ(added.size(), 16u);
    EXPECT_NE(added, KeyAt(0));
}

TEST_F(StableKeyEditingTest, ADuplicateGetsAKeyOfItsOwn)
{
    ASSERT_EQ(Properties->Act("$.data.attributes[0]", "duplicate", {}), PropertyEditResult::Applied);
    EXPECT_EQ(KeyAt(0), "ammo");
    EXPECT_NE(KeyAt(1), "ammo") << "a copy sharing the key would be selected, undone and referenced as the original";
    EXPECT_TRUE(IsValidStableKey(KeyAt(1)));
}

TEST(StableKey, MintedKeysAreSixteenHexDigitsAndDistinct)
{
    const std::string first = MintStableKey();
    const std::string second = MintStableKey();
    EXPECT_EQ(first.size(), 16u);
    EXPECT_TRUE(IsValidStableKey(first));
    EXPECT_NE(first, second);
}
