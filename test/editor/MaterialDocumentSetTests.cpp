#include "EditMaterialCommand.h"
#include "MaterialDocumentSet.h"

#include "documents/DocumentSourceSet.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// Open materials as documents in the application's journal: one tab per
// material, each edit a journal step, a save that refuses a file changed on
// disk, a close that asks what to do with a change and puts the resident
// material back when it is dropped, and a file changed on disk taken by a
// clean tab and held off by a changed one.
namespace
{
class MaterialDocumentSetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Dir = std::filesystem::temp_directory_path() / "sencha_material_documents_tests";
        std::filesystem::remove_all(Dir);
        std::filesystem::create_directories(Dir);
    }

    void TearDown() override
    {
        Sources.DiscardAll();
        std::filesystem::remove_all(Dir);
    }

    std::string WriteSmat(const char* name, const char* body = R"({"version": 2})")
    {
        const std::filesystem::path path = Dir / name;
        std::ofstream(path, std::ios::trunc) << body;
        return path.string();
    }

    void ChangeOnDisk(const std::string& file)
    {
        std::ofstream(file, std::ios::trunc) << R"({"version": 2, "metallic_factor": 1.0})";
        const auto later = std::filesystem::last_write_time(file) + std::chrono::seconds(2);
        std::filesystem::last_write_time(file, later);
    }

    void Edit(MaterialEditTab& tab, float metallic)
    {
        MaterialDescription after = tab.Session.Working();
        after.MetallicFactor = metallic;
        tab.Commands.Execute(std::make_unique<EditMaterialCommand>(tab.Session, tab.Session.Working(), after));
    }

    MaterialEditTab& Open(const char* name)
    {
        std::string error;
        MaterialEditTab* tab = Set.OpenOrFocus(std::string("asset://materials/") + name, WriteSmat(name), &error);
        EXPECT_NE(tab, nullptr) << error;
        return *tab;
    }

    std::filesystem::path Dir;
    DocumentSourceSet Sources;
    std::vector<float> Pushed;
    MaterialDocumentSet Set{ Sources, [this](MaterialEditTab&, const MaterialDescription& description) {
                                Pushed.push_back(description.MetallicFactor);
                            } };
};
}

TEST_F(MaterialDocumentSetTest, OpenCreatesTabsAndFocusReusesThem)
{
    (void)Open("a.smat");
    (void)Open("b.smat");
    EXPECT_EQ(Set.Tabs().size(), 2u);
    EXPECT_EQ(Set.ActiveIndex(), 1u);

    std::string error;
    ASSERT_NE(Set.OpenOrFocus("asset://materials/a.smat", "unused", &error), nullptr);
    EXPECT_EQ(Set.Tabs().size(), 2u) << "reopening an open material duplicated it";
    EXPECT_EQ(Set.ActiveIndex(), 0u);
}

TEST_F(MaterialDocumentSetTest, EditsAreJournalStepsAndAStepBringsItsTabForward)
{
    MaterialEditTab& a = Open("a.smat");
    MaterialEditTab& b = Open("b.smat");
    Edit(a, 0.25f);
    Edit(b, 0.75f);
    Set.SetActive(1);

    Sources.Undo();
    EXPECT_FALSE(b.Session.IsDirty());
    Sources.Undo();
    EXPECT_FALSE(a.Session.IsDirty());
    EXPECT_EQ(Set.ActiveIndex(), 0u) << "the tab the step landed on did not come forward";
    Sources.Redo();
    EXPECT_FLOAT_EQ(a.Session.Working().MetallicFactor, 0.25f);
}

TEST_F(MaterialDocumentSetTest, SaveAllWritesEveryChangedTabAndNothingElse)
{
    MaterialEditTab& a = Open("a.smat");
    (void)Open("b.smat");
    Edit(a, 0.25f);

    const DocumentSaveReport& report = Sources.SaveAll();
    EXPECT_EQ(report.Results.size(), 1u);
    EXPECT_TRUE(Sources.ChangedDocuments().empty());
}

TEST_F(MaterialDocumentSetTest, SavingOverAFileChangedOnDiskIsAConflict)
{
    MaterialEditTab& a = Open("a.smat");
    Edit(a, 0.25f);
    ChangeOnDisk(a.Session.FilePath());

    EXPECT_EQ(Sources.Save(Set.RefOf(a)).Status, DocumentSaveStatus::Conflict);
    std::string error;
    ASSERT_TRUE(Sources.Settle(Set.RefOf(a), ConflictChoice::TakeFile, error)) << error;
    EXPECT_FLOAT_EQ(a.Session.Working().MetallicFactor, 1.0f);
    EXPECT_FALSE(Sources.CanUndo()) << "the file's version left the old history retakeable";
}

TEST_F(MaterialDocumentSetTest, DroppingAChangedTabPutsTheResidentMaterialBack)
{
    MaterialEditTab& a = Open("a.smat");
    const float saved = a.Session.Saved().MetallicFactor;
    Edit(a, 0.25f);

    std::string error;
    EXPECT_FALSE(Set.Close(0, DirtyDisposition::Refuse, error));
    EXPECT_EQ(Set.Tabs().size(), 1u);
    ASSERT_TRUE(Set.Close(0, DirtyDisposition::Discard, error)) << error;
    EXPECT_TRUE(Set.Tabs().empty());
    ASSERT_EQ(Pushed.size(), 1u);
    EXPECT_FLOAT_EQ(Pushed[0], saved);
    EXPECT_FALSE(Sources.CanUndo());
}

TEST_F(MaterialDocumentSetTest, AFileChangedOnDiskIsTakenByACleanTabAndHeldByAChangedOne)
{
    MaterialEditTab& clean = Open("clean.smat");
    MaterialEditTab& changed = Open("changed.smat");
    Edit(changed, 0.25f);
    ChangeOnDisk(clean.Session.FilePath());
    ChangeOnDisk(changed.Session.FilePath());

    EXPECT_EQ(Sources.FileChangedOnDisk(clean.Session.FilePath()), ExternalChange::Adopted);
    EXPECT_FLOAT_EQ(clean.Session.Working().MetallicFactor, 1.0f);
    EXPECT_EQ(Sources.FileChangedOnDisk(changed.Session.FilePath()), ExternalChange::Held);
    EXPECT_FLOAT_EQ(changed.Session.Working().MetallicFactor, 0.25f) << "a held change was overwritten";
    EXPECT_EQ(Sources.FileChangedOnDisk(Dir / "elsewhere.smat"), ExternalChange::NotOpen);
}

TEST_F(MaterialDocumentSetTest, ItsOwnSaveIsNotAnOutsideChange)
{
    MaterialEditTab& a = Open("a.smat");
    Edit(a, 0.25f);
    ASSERT_EQ(Sources.Save(Set.RefOf(a)).Status, DocumentSaveStatus::Saved);
    EXPECT_EQ(Sources.FileChangedOnDisk(a.Session.FilePath()), ExternalChange::Adopted);
    EXPECT_FLOAT_EQ(a.Session.Working().MetallicFactor, 0.25f);
    EXPECT_TRUE(Sources.CanUndo()) << "the author's own save cost them their history";
}
