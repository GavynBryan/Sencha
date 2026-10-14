// An open data document follows its file when a file step moves it: its unsaved
// change and its history go with it, and undoing the move brings it back.

#include "data/DataDocumentStore.h"
#include "documents/DocumentSourceSet.h"
#include "project/ProjectFileSteps.h"

#include <anim/AnimFactSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <core/logging/LoggingProvider.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace
{
constexpr std::string_view kFacts = "asset://facts.sdata";
constexpr std::string_view kMoved = "asset://kept/facts.sdata";

struct DataDocumentMove : testing::Test
{
    std::filesystem::path Root = std::filesystem::temp_directory_path()
                                 / ("sencha_data_document_move_" + std::to_string(std::random_device{}()));
    LoggingProvider Logging;
    ComponentSerializerRegistry Serializers;
    RuntimeAssets Assets{ Logging, Serializers };
    DocumentSourceSet Sources;
    DataDocumentStore Store{ Assets, Sources };
    std::unique_ptr<ProjectFileSteps> Files;
    std::string Error;

    void SetUp() override
    {
        std::filesystem::create_directories(Root);
        std::ofstream(Root / "facts.sdata") << R"({ "id": "00000000000000f1", "type": "animation.fact_schema",
            "version": 1, "data": { "slots": [ { "name": "Crouched", "kind": "bool" } ] } })";
        (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
        Files = std::make_unique<ProjectFileSteps>(
            std::vector<std::filesystem::path>{ Root }, Sources, ProjectFileHooks{});
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        Files.reset();
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }

    [[nodiscard]] static std::string Slot(const DataDocument& document)
    {
        return document.Root().Find("data")->Find("slots")->AsArray()[0].Find("name")->AsString();
    }
};
} // namespace

TEST_F(DataDocumentMove, ItsChangeAndHistoryGoWithItsFile)
{
    DataDocument* document = Store.Open(kFacts, Error);
    ASSERT_NE(document, nullptr) << Error;
    Store.Hold(*document);
    JsonValue root = document->CopyRoot();
    root.Find("data")->Find("slots")->AsArray()[0].AsObject()[0].second = JsonValue(std::string("Sliding"));
    document->ReplaceRoot(std::move(root));
    Store.Changed(*document);

    ASSERT_EQ(Files->Move(kFacts, kMoved), "") << "an open file follows rather than refuses";
    EXPECT_EQ(document->VirtualPath(), kMoved);
    EXPECT_EQ(document->FilePath(), Root / "kept" / "facts.sdata");
    EXPECT_TRUE(document->IsDirty()) << "its unsaved change went with it";

    Sources.Undo();
    EXPECT_EQ(document->VirtualPath(), kFacts) << "undoing the move brings it back";
    EXPECT_TRUE(std::filesystem::exists(Root / "facts.sdata"));
    Sources.Undo();
    EXPECT_EQ(Slot(*document), "Crouched") << "the edit before the move is still its own";

    Sources.Redo();
    Sources.Redo();
    EXPECT_EQ(document->VirtualPath(), kMoved);
    EXPECT_EQ(Slot(*document), "Sliding");
    ASSERT_TRUE(document->Save(&Error)) << Error;
    std::ifstream in(Root / "kept" / "facts.sdata");
    const std::string saved{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    EXPECT_NE(saved.find("Sliding"), std::string::npos);
}
