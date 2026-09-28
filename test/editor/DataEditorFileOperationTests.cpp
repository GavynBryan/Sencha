// Renaming or deleting a data asset never drops an open document's changes
// unless the author chose to save or discard them.

#include "DataEditorWorkspace.h"

#include "project/Project.h"

#include <anim/AnimFactSchema.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonParser.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

namespace
{
    constexpr std::string_view kFacts = "asset://facts.sdata";

    struct DataEditorFiles : testing::Test
    {
        std::filesystem::path Root = std::filesystem::temp_directory_path()
            / ("sencha_data_editor_files_" + std::to_string(std::random_device{}()));
        LoggingProvider Logging;
        ComponentSerializerRegistry Serializers;
        RuntimeAssets Assets{ Logging, Serializers };
        ProjectDescriptor Project;
        std::unique_ptr<DataEditorWorkspace> Workspace;
        std::string Error;

        void SetUp() override
        {
            std::filesystem::create_directories(Root);
            std::ofstream(Root / "facts.sdata") << R"({ "type": "animation.fact_schema", "version": 1,
                "data": { "slots": [ { "name": "Crouched", "kind": "bool" } ] } })";
            (void)ScanAssetsDirectory(Root.generic_string(), Assets.Registry, Assets.Assets.Kinds());
            Project.ContentRoots = { Root.generic_string() };
            Workspace = std::make_unique<DataEditorWorkspace>(Assets, Project);
        }

        void TearDown() override
        {
            Workspace.reset();
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        DataDocument& OpenEdited()
        {
            DataDocument* document = Workspace->Documents.OpenOrFocus(kFacts, Error);
            EXPECT_NE(document, nullptr) << Error;
            JsonValue root = document->CopyRoot();
            root.Find("data")->Find("slots")->AsArray()[0].AsObject()[0].second = JsonValue(std::string("Sliding"));
            document->ReplaceRoot(std::move(root));
            Workspace->Documents.Changed(*document);
            return *document;
        }

        [[nodiscard]] std::string SlotIn(const std::string& name) const
        {
            std::ifstream in(Root / name);
            const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
            return JsonParse(text)->Find("data")->Find("slots")->AsArray()[0].Find("name")->AsString();
        }
    };
}

TEST_F(DataEditorFiles, RenameRefusesAChangedDocumentUnlessTold)
{
    (void)OpenEdited();
    EXPECT_FALSE(Workspace->Rename(kFacts, "moved", DirtyDisposition::Refuse, Error));
    EXPECT_TRUE(std::filesystem::exists(Root / "facts.sdata"));
    EXPECT_FALSE(std::filesystem::exists(Root / "moved.sdata"));
    ASSERT_NE(Workspace->Documents.Find(kFacts), nullptr);
    EXPECT_TRUE(Workspace->Documents.Find(kFacts)->IsDirty());

    ASSERT_TRUE(Workspace->Rename(kFacts, "moved", DirtyDisposition::Save, Error)) << Error;
    EXPECT_EQ(SlotIn("moved.sdata"), "Sliding");
    EXPECT_EQ(Workspace->Documents.Find(kFacts), nullptr);
    ASSERT_NE(Workspace->Documents.Find("asset://moved.sdata"), nullptr);
    EXPECT_FALSE(Workspace->Sources.CanUndo()) << "the old document's steps are forgotten";
}

TEST_F(DataEditorFiles, RenameDiscardingMovesTheSavedFile)
{
    (void)OpenEdited();
    ASSERT_TRUE(Workspace->Rename(kFacts, "moved", DirtyDisposition::Discard, Error)) << Error;
    EXPECT_EQ(SlotIn("moved.sdata"), "Crouched");
    EXPECT_FALSE(Workspace->Documents.Find("asset://moved.sdata")->IsDirty());
    EXPECT_FALSE(Workspace->Sources.CanUndo());
}

TEST_F(DataEditorFiles, DeleteRefusesAChangedDocumentUnlessTold)
{
    (void)OpenEdited();
    EXPECT_FALSE(Workspace->Delete(kFacts, DirtyDisposition::Refuse, Error));
    EXPECT_TRUE(std::filesystem::exists(Root / "facts.sdata"));
    EXPECT_NE(Workspace->Documents.Find(kFacts), nullptr);

    ASSERT_TRUE(Workspace->Delete(kFacts, DirtyDisposition::Discard, Error)) << Error;
    EXPECT_FALSE(std::filesystem::exists(Root / "facts.sdata"));
    EXPECT_EQ(Workspace->Documents.Find(kFacts), nullptr);
    EXPECT_FALSE(Assets.Registry.Contains(kFacts));
    EXPECT_FALSE(Workspace->Sources.CanUndo());
}

TEST_F(DataEditorFiles, AnUnchangedDocumentNeedsNoChoice)
{
    ASSERT_NE(Workspace->Documents.OpenOrFocus(kFacts, Error), nullptr) << Error;
    EXPECT_TRUE(Workspace->Delete(kFacts, DirtyDisposition::Refuse, Error)) << Error;
    EXPECT_EQ(Workspace->Documents.Find(kFacts), nullptr);
}
