// The open data documents of one editor: nothing with changes is dropped unless
// the caller chose to, switching documents keeps a typed edit, and settling a
// conflict leaves the document, the resident asset and the file agreeing.

#include "data/DataDocumentSet.h"

#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    constexpr std::string_view kGame = "asset://animation/game.facts.sdata";
    constexpr std::string_view kOther = "asset://animation/other.facts.sdata";

    struct FactDocuments : testing::Test
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("sencha_data_document_set_" + std::to_string(std::random_device{}()));
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        DocumentSourceSet Sources;
        std::unique_ptr<DataDocumentSet> Set;
        int Notified = 0;
        int ResidentChanges = 0;

        void SetUp() override
        {
            std::filesystem::create_directories(Root / "animation");
            WriteFacts("game.facts.sdata", "Crouched");
            WriteFacts("other.facts.sdata", "Wet");
            std::ofstream(Root / "animation" / "requests.sdata")
                << R"({ "type": "animation.request_schema", "version": 1, "data": { "intents": [] } })";
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
            Set = std::make_unique<DataDocumentSet>(Assets, Sources,
                DataDocumentSetConfig{ .ContentRoot = Root, .Subtypes = { std::string(kAnimFactSchemaType) } });
            Set->OnChanged([this](DataDocument&, bool residentChanged) {
                ++Notified;
                ResidentChanges += residentChanged ? 1 : 0;
            });
        }

        void TearDown() override
        {
            Set.reset();
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        void WriteFacts(const std::string& name, std::string_view slot) const
        {
            std::ofstream(Root / "animation" / name, std::ios::trunc)
                << R"({ "type": "animation.fact_schema", "version": 1, "data": { "slots": [ { "name": ")"
                << slot << R"(", "kind": "bool" } ] } })";
        }

        // Stamped later so the timestamp alone cannot miss it.
        void ChangeOnDisk(const std::string& name, std::string_view slot) const
        {
            WriteFacts(name, slot);
            const std::filesystem::path file = Root / "animation" / name;
            std::filesystem::last_write_time(file, std::filesystem::last_write_time(file) + std::chrono::seconds(5));
        }

        DataDocument& OpenFacts(std::string_view path)
        {
            std::string error;
            DataDocument* document = Set->OpenOrFocus(path, error);
            EXPECT_NE(document, nullptr) << error;
            return *document;
        }

        static JsonValue WithSlot(const DataDocument& document, std::string_view slot)
        {
            JsonValue root = document.CopyRoot();
            root.Find("data")->Find("slots")->AsArray()[0].AsObject()[0].second = JsonValue(std::string(slot));
            return root;
        }

        void Commit(DataDocument& document, std::string_view slot)
        {
            document.ReplaceRoot(WithSlot(document, slot));
            Set->Changed(document);
        }

        [[nodiscard]] static std::string SlotOf(const DataDocument& document)
        {
            return document.Data()->Find("slots")->AsArray()[0].Find("name")->AsString();
        }

        [[nodiscard]] std::string SlotOnDisk(const std::string& name) const
        {
            std::ifstream in(Root / "animation" / name);
            const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
            return JsonParse(text)->Find("data")->Find("slots")->AsArray()[0].Find("name")->AsString();
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
    };
}

TEST_F(FactDocuments, OpensEachDocumentOnceAndOnlyItsSubtypes)
{
    DataDocument& game = OpenFacts(kGame);
    (void)OpenFacts(kOther);
    EXPECT_EQ(&OpenFacts(kGame), &game);
    EXPECT_EQ(Set->Documents().size(), 2u);
    EXPECT_EQ(Set->Active(), &game);

    std::string error;
    EXPECT_EQ(Set->OpenOrFocus("asset://animation/requests.sdata", error), nullptr);
    EXPECT_NE(error.find("does not open"), std::string::npos) << error;
    EXPECT_EQ(Set->Create(kAnimRequestSchemaType, "animation/more", error), nullptr);
}

TEST_F(FactDocuments, CreateWritesRegistersAndOpens)
{
    std::string error;
    DataDocument* created = Set->Create(kAnimFactSchemaType, "animation/new.facts", error);
    ASSERT_NE(created, nullptr) << error;
    EXPECT_EQ(created->VirtualPath(), "asset://animation/new.facts.sdata");
    EXPECT_TRUE(std::filesystem::exists(Root / "animation" / "new.facts.sdata"));
    EXPECT_TRUE(Assets.Registry.Contains(created->VirtualPath()));
    EXPECT_EQ(Set->Active(), created);
    EXPECT_EQ(Set->Create(kAnimFactSchemaType, "animation/new.facts.sdata", error), nullptr);
    EXPECT_NE(error.find("already exists"), std::string::npos) << error;
}

TEST_F(FactDocuments, CloseRefusesAChangedDocumentUnlessTold)
{
    AssetLease game = Load(kGame);
    DataDocument& document = OpenFacts(kGame);
    Commit(document, "Sliding");
    ASSERT_EQ(ResidentSlot(kGame), "Sliding");

    std::string error;
    EXPECT_FALSE(Set->Close(0, DirtyDisposition::Refuse, error));
    EXPECT_NE(error.find("unsaved"), std::string::npos) << error;
    EXPECT_EQ(Set->Documents().size(), 1u);

    ASSERT_TRUE(Set->Close(0, DirtyDisposition::Discard, error)) << error;
    EXPECT_TRUE(Set->Documents().empty());
    EXPECT_FALSE(Sources.CanUndo()) << "the closed document's steps are forgotten";
    EXPECT_EQ(ResidentSlot(kGame), "Crouched") << "the resident asset goes back to the file";
    EXPECT_EQ(SlotOnDisk("game.facts.sdata"), "Crouched");
}

TEST_F(FactDocuments, CloseSavesWhenToldAndRefusesAConflict)
{
    DataDocument& game = OpenFacts(kGame);
    Commit(game, "Sliding");
    std::string error;
    ASSERT_TRUE(Set->Close(0, DirtyDisposition::Save, error)) << error;
    EXPECT_EQ(SlotOnDisk("game.facts.sdata"), "Sliding");

    DataDocument& other = OpenFacts(kOther);
    Commit(other, "Dry");
    ChangeOnDisk("other.facts.sdata", "Damp");
    EXPECT_FALSE(Set->Close(0, DirtyDisposition::Save, error));
    EXPECT_NE(error.find("changed on disk"), std::string::npos) << error;
    EXPECT_EQ(Set->Documents().size(), 1u);
    EXPECT_EQ(SlotOnDisk("other.facts.sdata"), "Damp");
}

TEST_F(FactDocuments, ReloadRefusesChangesAndForgetsTheHistoryItClears)
{
    DataDocument& game = OpenFacts(kGame);
    Commit(game, "Sliding");
    std::string error;
    EXPECT_FALSE(Set->Reload(game, error));

    ASSERT_EQ(Sources.Save(Set->RefOf(game)).Status, DocumentSaveStatus::Saved);
    ASSERT_TRUE(Sources.CanUndo());
    ASSERT_TRUE(Set->Reload(game, error)) << error;
    EXPECT_FALSE(Sources.CanUndo()) << "no step is left that the document can no longer take";
}

TEST_F(FactDocuments, SwitchingCommitsATypedEditOnceNotifiesOnceAndPushesOnce)
{
    AssetLease game = Load(kGame);
    DataDocument& other = OpenFacts(kOther);
    DataDocument& typed = OpenFacts(kGame);
    typed.BeginEdit();
    typed.PreviewRoot(WithSlot(typed, "Sliding"));
    const uint64_t reloads = Reloads(kGame);
    Notified = ResidentChanges = 0;

    Set->SetActive(*Set->IndexOf(kOther));
    EXPECT_EQ(Set->Active(), &other);
    EXPECT_FALSE(typed.IsEditing());
    EXPECT_EQ(SlotOf(typed), "Sliding");
    EXPECT_EQ(Notified, 1);
    EXPECT_EQ(ResidentChanges, 1);
    EXPECT_EQ(Reloads(kGame), reloads + 1);
    EXPECT_EQ(ResidentSlot(kGame), "Sliding");

    Sources.Undo();
    EXPECT_EQ(SlotOf(typed), "Crouched") << "the typed edit is one step";
    EXPECT_EQ(Set->Active(), &typed) << "a stepped document comes forward";
    EXPECT_FALSE(Sources.CanUndo());
}

TEST_F(FactDocuments, TakingTheFilesVersionBringsTheResidentAssetAndBaselineAlong)
{
    AssetLease game = Load(kGame);
    DataDocument& document = OpenFacts(kGame);
    Commit(document, "Sliding");
    ChangeOnDisk("game.facts.sdata", "Swimming");
    ASSERT_EQ(Sources.SaveAll().WithStatus(DocumentSaveStatus::Conflict).size(), 1u);

    std::string error;
    ASSERT_TRUE(Sources.Settle(Set->RefOf(document), ConflictChoice::TakeFile, error)) << error;
    EXPECT_EQ(SlotOf(document), "Swimming");
    EXPECT_FALSE(document.IsDirty());
    EXPECT_EQ(ResidentSlot(kGame), "Swimming");
    EXPECT_TRUE(Sources.LastSave().WithStatus(DocumentSaveStatus::Conflict).empty());
    EXPECT_EQ(Sources.Save(Set->RefOf(document)).Status, DocumentSaveStatus::Saved);

    Sources.Undo();
    EXPECT_EQ(SlotOf(document), "Sliding") << "the author's version stays one step away";
    EXPECT_EQ(ResidentSlot(kGame), "Sliding");
}

TEST_F(FactDocuments, KeepingMineOverwritesAndTheNextSaveDoesNotConflict)
{
    DataDocument& document = OpenFacts(kGame);
    Commit(document, "Sliding");
    ChangeOnDisk("game.facts.sdata", "Swimming");
    ASSERT_EQ(Sources.SaveAll().WithStatus(DocumentSaveStatus::Conflict).size(), 1u);

    std::string error;
    ASSERT_TRUE(Sources.Settle(Set->RefOf(document), ConflictChoice::KeepMine, error)) << error;
    EXPECT_EQ(SlotOnDisk("game.facts.sdata"), "Sliding");
    Commit(document, "Crawling");
    EXPECT_EQ(Sources.Save(Set->RefOf(document)).Status, DocumentSaveStatus::Saved);
    EXPECT_EQ(SlotOnDisk("game.facts.sdata"), "Crawling");
}

TEST_F(FactDocuments, TheJournalStepsAcrossDocumentsNewestFirst)
{
    DataDocument& game = OpenFacts(kGame);
    DataDocument& other = OpenFacts(kOther);
    Commit(game, "Sliding");
    Commit(other, "Dry");
    Sources.Undo();
    EXPECT_EQ(SlotOf(other), "Wet");
    EXPECT_EQ(SlotOf(game), "Sliding");
    Sources.Undo();
    EXPECT_EQ(SlotOf(game), "Crouched");
    EXPECT_EQ(Set->Active(), &game);
    Sources.Redo();
    EXPECT_EQ(SlotOf(game), "Sliding");
}
