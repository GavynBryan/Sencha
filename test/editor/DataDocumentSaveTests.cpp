// Saving a data document never overwrites a file someone else changed unless asked,
// and every way of settling that refreshes what "changed" is measured against.

#include "data/DataDocument.h"

#include <anim/AnimRequestSchema.h>
#include <core/json/JsonFormat.h>
#include <core/json/JsonParser.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    constexpr std::string_view kFile = R"({ "type": "animation.request_schema", "version": 1,
        "data": { "intents": [ { "intent": "Anim.Wave", "params": [] } ] } })";

    struct DataDocumentSave : testing::Test
    {
        std::filesystem::path Dir = std::filesystem::temp_directory_path()
            / ("sencha_data_document_save_" + std::to_string(std::random_device{}()));
        std::filesystem::path File = Dir / "requests.sdata";
        DataAssetTypeRegistry Types;
        DataSchemaRegistry Schemas;
        std::unique_ptr<DataDocument> Document;

        void SetUp() override
        {
            RegisterAnimRequestSchema(Types, Schemas);
            std::filesystem::create_directories(Dir);
            WriteFile(kFile);
            std::string error;
            Document = DataDocument::Open(File, "asset://requests.sdata", Types, Schemas, &error);
            ASSERT_NE(Document, nullptr) << error;
        }

        void TearDown() override
        {
            std::error_code ec;
            std::filesystem::remove_all(Dir, ec);
        }

        void WriteFile(std::string_view text) const
        {
            std::ofstream(File, std::ios::trunc) << text;
        }

        // Someone else's edit, stamped later so the timestamp alone cannot miss it.
        void ChangeOnDisk(std::string_view intent) const
        {
            JsonValue root = *JsonParse(kFile);
            root.Find("data")->Find("intents")->AsArray()[0].AsObject()[0].second = JsonValue(std::string(intent));
            WriteFile(JsonFormat(root));
            std::filesystem::last_write_time(File, std::filesystem::last_write_time(File) + std::chrono::seconds(5));
        }

        void EditIntent(std::string_view intent)
        {
            JsonValue root = Document->CopyRoot();
            root.Find("data")->Find("intents")->AsArray()[0].AsObject()[0].second = JsonValue(std::string(intent));
            Document->ReplaceRoot(std::move(root));
        }

        [[nodiscard]] std::string IntentInWorkingCopy() const
        {
            return Document->Data()->Find("intents")->AsArray()[0].Find("intent")->AsString();
        }

        [[nodiscard]] std::string IntentOnDisk() const
        {
            std::ifstream in(File);
            const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
            return JsonParse(text)->Find("data")->Find("intents")->AsArray()[0].Find("intent")->AsString();
        }
    };
}

TEST_F(DataDocumentSave, ATouchIsNotAChangeButNewContentIs)
{
    std::filesystem::last_write_time(File, std::filesystem::last_write_time(File) + std::chrono::seconds(5));
    EXPECT_FALSE(Document->IsExternallyModified()) << "same bytes, new timestamp";
    ChangeOnDisk("Anim.Theirs");
    EXPECT_TRUE(Document->IsExternallyModified());
}

TEST_F(DataDocumentSave, SaveRefusesAFileChangedOnDisk)
{
    EditIntent("Anim.Mine");
    ChangeOnDisk("Anim.Theirs");
    std::string error;
    EXPECT_FALSE(Document->Save(&error));
    EXPECT_NE(error.find("changed on disk"), std::string::npos) << error;
    EXPECT_EQ(IntentOnDisk(), "Anim.Theirs") << "their change is left alone";
    EXPECT_TRUE(Document->IsDirty());
}

TEST_F(DataDocumentSave, KeepingMineWritesAndTheNextSaveDoesNotConflict)
{
    EditIntent("Anim.Mine");
    ChangeOnDisk("Anim.Theirs");
    std::string error;
    ASSERT_TRUE(Document->SaveOverFile(&error)) << error;
    EXPECT_EQ(IntentOnDisk(), "Anim.Mine");
    EXPECT_FALSE(Document->IsExternallyModified());
    EditIntent("Anim.MineAgain");
    EXPECT_TRUE(Document->Save(&error)) << error;
    EXPECT_EQ(IntentOnDisk(), "Anim.MineAgain");
}

TEST_F(DataDocumentSave, TakingTheFilesIsOneStepAndTheNextSaveDoesNotConflict)
{
    EditIntent("Anim.Mine");
    ChangeOnDisk("Anim.Theirs");
    std::string error;
    ASSERT_TRUE(Document->AdoptFileVersion(Types, Schemas, &error)) << error;
    EXPECT_EQ(IntentInWorkingCopy(), "Anim.Theirs");
    EXPECT_FALSE(Document->IsDirty());
    EXPECT_FALSE(Document->IsExternallyModified());
    EXPECT_TRUE(Document->Save(&error)) << error;

    Document->Undo();
    EXPECT_EQ(IntentInWorkingCopy(), "Anim.Mine") << "the author's edit is one undo away";
}

TEST_F(DataDocumentSave, SaveCommitsAnOpenEditAsOneStep)
{
    JsonValue root = Document->CopyRoot();
    root.Find("data")->Find("intents")->AsArray()[0].AsObject()[0].second = JsonValue(std::string("Anim.Typed"));
    Document->BeginEdit();
    Document->PreviewRoot(std::move(root));
    std::string error;
    ASSERT_TRUE(Document->Save(&error)) << error;
    EXPECT_FALSE(Document->IsEditing());
    EXPECT_EQ(IntentOnDisk(), "Anim.Typed");
    ASSERT_TRUE(Document->CanUndo());
    Document->Undo();
    EXPECT_EQ(IntentInWorkingCopy(), "Anim.Wave");
}
