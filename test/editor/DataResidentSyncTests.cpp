// A resident data asset follows its document's latest committed version, never a
// preview, never a version the document has since moved past, and never by waiting.

#include "data/DataDocument.h"
#include "data/DataResidentSync.h"

#include <anim/AnimFactSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    constexpr std::string_view kGame = "asset://animation/game.facts.sdata";
    constexpr std::string_view kOther = "asset://animation/other.facts.sdata";

    struct ResidentFacts : testing::Test
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("sencha_data_resident_sync_" + std::to_string(std::random_device{}()));
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        DataResidentSync Sync{ Assets };
        std::unique_ptr<DataDocument> Game;
        std::unique_ptr<DataDocument> Other;

        void SetUp() override
        {
            std::filesystem::create_directories(Root / "animation");
            Write("base.facts.sdata", "", "Grounded");
            Write("game.facts.sdata", "asset://animation/base.facts.sdata", "Crouched");
            Write("other.facts.sdata", "", "Wet");
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
            Game = Open(kGame);
            Other = Open(kOther);
        }

        void TearDown() override
        {
            Sync.Forget(*Game);
            Sync.Forget(*Other);
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        [[nodiscard]] static std::string Envelope(std::string_view extends, std::string_view slot)
        {
            const std::string extendsField = extends.empty() ? "" : "\"extends\": \"" + std::string(extends) + "\", ";
            return R"({ "type": "animation.fact_schema", "version": 1, "data": { )" + extendsField
                + R"("slots": [ { "name": ")" + std::string(slot) + R"(", "kind": "bool" } ] } })";
        }

        void Write(const std::string& name, std::string_view extends, std::string_view slot) const
        {
            std::ofstream(Root / "animation" / name, std::ios::trunc) << Envelope(extends, slot);
        }

        std::unique_ptr<DataDocument> Open(std::string_view path)
        {
            const AssetRecord* record = Assets.Registry.FindByPath(path);
            std::string error;
            auto document = DataDocument::Open(record->FilePath, std::string(path), Assets.DataTypes,
                                               Assets.DataSchemas, &error);
            EXPECT_NE(document, nullptr) << error;
            return document;
        }

        static JsonValue WithSlot(const DataDocument& document, std::string_view slot)
        {
            JsonValue root = document.CopyRoot();
            root.Find("data")->Find("slots")->AsArray()[0].AsObject()[0].second = JsonValue(std::string(slot));
            return root;
        }

        static void Commit(DataDocument& document, std::string_view slot)
        {
            document.ReplaceRoot(WithSlot(document, slot));
        }

        [[nodiscard]] std::string ResidentSlot(std::string_view path) const
        {
            const AnimFactSchema* facts =
                Assets.DataAssets.TryGet<AnimFactSchema>(Assets.DataAssets.Find(path), kAnimFactSchemaType);
            return facts == nullptr || facts->Slots.empty() ? "" : facts->Slots.back().Name;
        }

        [[nodiscard]] uint64_t Reloads(std::string_view path) const
        {
            return Assets.DataAssets.GetReloadVersion(Assets.DataAssets.Find(path));
        }

        [[nodiscard]] AssetLease Load(std::string_view path)
        {
            return Assets.Assets.LoadLease(path, AssetType::Data);
        }

        [[nodiscard]] DataResidentStatus Status(const DataDocument& document) const
        {
            const DataResidentState* state = Sync.StateOf(document);
            EXPECT_NE(state, nullptr);
            return state == nullptr ? DataResidentStatus::Pending : state->Status;
        }
    };
}

TEST_F(ResidentFacts, APushAppliesAtOnceWhenResident)
{
    AssetLease game = Load(kGame);
    Commit(*Game, "Sliding");
    EXPECT_TRUE(Sync.Push(*Game));
    EXPECT_EQ(ResidentSlot(kGame), "Sliding");
    EXPECT_EQ(Status(*Game), DataResidentStatus::Current);
    EXPECT_TRUE(Game->IsDirty()) << "the file is not the resident asset";
}

TEST_F(ResidentFacts, APushWaitsForResidencyAndAppliesOnce)
{
    Commit(*Game, "Sliding");
    EXPECT_FALSE(Sync.Push(*Game));
    EXPECT_EQ(Status(*Game), DataResidentStatus::Pending);

    AssetLease game = Load(kGame);
    EXPECT_EQ(ResidentSlot(kGame), "Crouched") << "a load reads the file";
    EXPECT_TRUE(Sync.PushWaiting());
    EXPECT_EQ(ResidentSlot(kGame), "Sliding");
    EXPECT_EQ(Status(*Game), DataResidentStatus::Current);
    EXPECT_FALSE(Sync.PushWaiting());
}

TEST_F(ResidentFacts, OnlyTheLatestCommitAppliesAndOnlyOnce)
{
    Commit(*Game, "Sliding");
    (void)Sync.Push(*Game);
    Commit(*Game, "Swimming");
    (void)Sync.Push(*Game);

    AssetLease game = Load(kGame);
    const uint64_t loaded = Reloads(kGame);
    EXPECT_TRUE(Sync.PushWaiting());
    EXPECT_EQ(ResidentSlot(kGame), "Swimming");
    EXPECT_EQ(Reloads(kGame), loaded + 1);
}

TEST_F(ResidentFacts, AVersionTheDocumentMovedPastIsNeverApplied)
{
    Commit(*Game, "Sliding");
    (void)Sync.Push(*Game);
    Game->Undo();

    AssetLease game = Load(kGame);
    const uint64_t loaded = Reloads(kGame);
    EXPECT_FALSE(Sync.PushWaiting());
    EXPECT_EQ(Reloads(kGame), loaded);
    EXPECT_EQ(Sync.StateOf(*Game), nullptr);
}

TEST_F(ResidentFacts, ReloadOrForgetWhilePendingAppliesNothing)
{
    Commit(*Game, "Sliding");
    (void)Sync.Push(*Game);
    ASSERT_TRUE(Game->Reload(Assets.DataTypes, Assets.DataSchemas));

    Commit(*Other, "Dry");
    (void)Sync.Push(*Other);
    Sync.Forget(*Other);

    AssetLease game = Load(kGame);
    AssetLease other = Load(kOther);
    EXPECT_FALSE(Sync.PushWaiting());
    EXPECT_EQ(ResidentSlot(kGame), "Crouched");
    EXPECT_EQ(ResidentSlot(kOther), "Wet");
}

TEST_F(ResidentFacts, APreviewIsNeverPushed)
{
    AssetLease game = Load(kGame);
    Commit(*Game, "Sliding");
    Game->BeginEdit();
    Game->PreviewRoot(WithSlot(*Game, "Swimming"));
    EXPECT_TRUE(Sync.Push(*Game));
    EXPECT_EQ(ResidentSlot(kGame), "Sliding");
    Game->CancelEdit();
}

TEST_F(ResidentFacts, ARefusedVersionKeepsTheLastValidOne)
{
    AssetLease game = Load(kGame);
    Commit(*Game, "not a fact name");
    EXPECT_FALSE(Sync.Push(*Game));
    EXPECT_EQ(Status(*Game), DataResidentStatus::KeptLastValid);
    EXPECT_FALSE(Sync.StateOf(*Game)->Error.empty());
    EXPECT_EQ(ResidentSlot(kGame), "Crouched");
}

TEST_F(ResidentFacts, AnAppliedVersionReleasesAPushWaitingOnWhatItLoads)
{
    AssetLease game = Load(kGame);
    Commit(*Other, "Dry");
    EXPECT_FALSE(Sync.Push(*Other));

    JsonValue root = Game->CopyRoot();
    root.Find("data")->AsObject()[0].second = JsonValue(std::string(kOther));
    Game->ReplaceRoot(std::move(root));
    EXPECT_TRUE(Sync.Push(*Game));
    EXPECT_EQ(ResidentSlot(kOther), "Dry");
    EXPECT_EQ(Status(*Other), DataResidentStatus::Current);
}

TEST_F(ResidentFacts, RestoringFromFileDropsTheWorkingVersion)
{
    AssetLease game = Load(kGame);
    Commit(*Game, "Sliding");
    ASSERT_TRUE(Sync.Push(*Game));
    Sync.RestoreFromFile(*Game);
    EXPECT_EQ(ResidentSlot(kGame), "Crouched");
    EXPECT_EQ(Sync.StateOf(*Game), nullptr);
}
