#include "project/CookProfile.h"
#include "project/Project.h"
#include "project/CookProfileEditor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>

// The project's cook profiles, edited as properties: built-ins are inherited
// and never written, a duplicate is how one is changed, and nothing reaches the
// project file until Save.
namespace
{
class CookProfileEditorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Dir = std::filesystem::temp_directory_path() / ("sencha_cook_profile_editor_" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(Dir);
        Project.Directory = Dir.generic_string();
        CookProfile nightly;
        nightly.Id = "nightly";
        nightly.Name = "Nightly";
        nightly.TargetSteps = { std::string(CookStepIds::RenderMeshes) };
        Project.CookProfiles.push_back(std::move(nightly));
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(Dir, ec);
    }

    [[nodiscard]] std::size_t IndexOf(std::string_view id) const
    {
        const std::vector<CookProfile> profiles = Editor.Profiles();
        return static_cast<std::size_t>(std::ranges::find(profiles, id, &CookProfile::Id) - profiles.begin());
    }

    PropertyEditResult Commit(std::string_view key, std::string_view value)
    {
        if (!Editor.BeginEdit(key))
            return PropertyEditResult::Refused;
        return Editor.CommitEdit(key, value);
    }

    std::filesystem::path Dir;
    ProjectDescriptor Project;
    CookProfileEditor Editor{ Project };
};
} // namespace

TEST_F(CookProfileEditorTest, ABuiltInReadsAsPropertiesNoneOfWhichCanBeWritten)
{
    Editor.Select(IndexOf("full"));
    ASSERT_TRUE(Editor.SelectedIsBuiltIn());
    for (const PropertyNode& node : Editor.Nodes())
        EXPECT_TRUE(node.Descriptor.ReadOnly) << node.Key;
    EXPECT_EQ(Commit("name", "Mine"), PropertyEditResult::Refused);
    EXPECT_FALSE(Editor.Delete());
    EXPECT_FALSE(Editor.Changed());
}

TEST_F(CookProfileEditorTest, EditingAnOwnedProfileChangesTheProjectButNotTheFile)
{
    Editor.Select(IndexOf("nightly"));
    EXPECT_EQ(Commit("name", "Nightly Build"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit("step.collision", "true"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit("output.structure", "Withdraw"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit("output.structure", "Sometimes"), PropertyEditResult::Refused);

    const CookProfile& owned = Project.CookProfiles.front();
    EXPECT_EQ(owned.Name, "Nightly Build");
    EXPECT_NE(std::ranges::find(owned.TargetSteps, CookStepIds::Collision), owned.TargetSteps.end());
    ASSERT_EQ(owned.OutputPolicies.size(), 1u);
    EXPECT_EQ(owned.OutputPolicies.front().Disposition, CookOutputDisposition::Withdraw);
    EXPECT_TRUE(Editor.Changed());
    EXPECT_FALSE(std::filesystem::exists(Dir / "project.senchaproj"));
}

TEST_F(CookProfileEditorTest, CancellingAnEditPutsBackWhatItBeganFrom)
{
    Editor.Select(IndexOf("nightly"));
    ASSERT_TRUE(Editor.BeginEdit("name"));
    EXPECT_EQ(Editor.PreviewEdit("name", "Half Typed"), PropertyEditResult::Applied);
    Editor.CancelEdit("name");
    EXPECT_EQ(Project.CookProfiles.front().Name, "Nightly");
}

TEST_F(CookProfileEditorTest, DuplicatingABuiltInMakesAnOwnedCopyWithItsOwnId)
{
    Editor.Select(IndexOf("full"));
    Editor.Duplicate();
    Editor.Select(IndexOf("full"));
    Editor.Duplicate();
    ASSERT_EQ(Project.CookProfiles.size(), 3u);
    EXPECT_NE(Project.CookProfiles[1].Id, Project.CookProfiles[2].Id);
    EXPECT_FALSE(Project.CookProfiles[2].BuiltIn);
    EXPECT_EQ(Editor.Profiles()[Editor.Selected()].Id, Project.CookProfiles[2].Id) << "the copy is selected";
    EXPECT_EQ(Commit("name", "Tweaked"), PropertyEditResult::Applied);
}

TEST_F(CookProfileEditorTest, DeletingAnOwnedProfileRemovesItAndSelectsAnother)
{
    Editor.Select(IndexOf("nightly"));
    const std::uint64_t before = Editor.Revision();
    EXPECT_TRUE(Editor.Delete());
    EXPECT_TRUE(Project.CookProfiles.empty());
    EXPECT_GT(Editor.Revision(), before);
    EXPECT_LT(Editor.Selected(), Editor.Profiles().size());
}

TEST_F(CookProfileEditorTest, SaveWritesThroughTheProjectAndClearsTheChange)
{
    Editor.Select(IndexOf("nightly"));
    ASSERT_EQ(Commit("name", "Saved Name"), PropertyEditResult::Applied);
    ASSERT_TRUE(Editor.Save()) << Editor.Status();
    EXPECT_FALSE(Editor.Changed());
    ProjectDescriptor reloaded;
    ASSERT_TRUE(ProjectDescriptor::Load((Dir / "project.senchaproj").string(), reloaded, nullptr));
    ASSERT_EQ(reloaded.CookProfiles.size(), 1u);
    EXPECT_EQ(reloaded.CookProfiles.front().Name, "Saved Name");
}

TEST_F(CookProfileEditorTest, ADuplicateIdStopsTheSaveAndSaysWhy)
{
    CookProfile clash = Project.CookProfiles.front();
    Project.CookProfiles.push_back(clash);
    EXPECT_FALSE(Editor.Validate());
    EXPECT_NE(Editor.Status().find("Duplicate"), std::string::npos);
    EXPECT_FALSE(Editor.Save());
    EXPECT_FALSE(std::filesystem::exists(Dir / "project.senchaproj"));
}
