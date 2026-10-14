#include "ProjectCreation.h"

#include <gtest/gtest.h>

// A new project as properties: its directory, name and template, created by
// the one verb, which needs a directory and says why when creation fails.
namespace
{
struct Created
{
    std::string Directory;
    std::string Name;
    std::string Template;
};

PropertyEditResult Commit(ProjectCreation& form, std::string_view key, std::string_view value)
{
    return form.BeginEdit(key) ? form.CommitEdit(key, value) : PropertyEditResult::Refused;
}
} // namespace

TEST(ProjectCreation, TheVerbCreatesWhatTheFormSays)
{
    std::vector<Created> made;
    ProjectCreation form({ "fps", "topdown" }, [&](const std::string& directory, const std::string& name,
                                                    const std::string& templateName, std::string&) {
        made.push_back({ directory, name, templateName });
        return true;
    });
    EXPECT_EQ(form.Act("project", "create", {}), PropertyEditResult::Refused) << "no directory yet";
    EXPECT_EQ(Commit(form, "directory", "/work/matcha"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit(form, "template", "topdown"), PropertyEditResult::Applied);
    EXPECT_EQ(form.Act("project", "create", {}), PropertyEditResult::Applied);
    ASSERT_EQ(made.size(), 1u);
    EXPECT_EQ(made[0].Directory, "/work/matcha");
    EXPECT_EQ(made[0].Template, "topdown");
}

TEST(ProjectCreation, NoTemplateIsAChoiceAndAFailureIsSaid)
{
    std::string asked = "unset";
    ProjectCreation form({ "fps" }, [&](const std::string&, const std::string&, const std::string& templateName,
                                        std::string& error) {
        asked = templateName;
        error = "directory is not empty";
        return false;
    });
    EXPECT_EQ(Commit(form, "directory", "/work/full"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit(form, "template", "None"), PropertyEditResult::Applied);
    EXPECT_EQ(Commit(form, "template", "nonsense"), PropertyEditResult::Refused);
    EXPECT_EQ(form.Act("project", "create", {}), PropertyEditResult::Refused);
    EXPECT_EQ(asked, "");
    bool said = false;
    const std::vector<PropertyNode> nodes = form.Nodes();
    for (const PropertyNode& child : nodes.front().Children)
        said |= child.Key == "problem" && child.Value == "directory is not empty";
    EXPECT_TRUE(said);
}
