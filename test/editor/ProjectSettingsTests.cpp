#include "ProjectSettings.h"

#include <gtest/gtest.h>

#include <algorithm>

// A project's settings as properties: each change is saved at once, a relative
// content root is taken from the project's directory, and a save that fails
// keeps what was there and says why.
namespace
{
ProjectDescriptor Descriptor()
{
    ProjectDescriptor descriptor;
    descriptor.Name = "Tea";
    descriptor.Directory = "/work/tea";
    descriptor.GameModulePath = "/work/tea/build/game.so";
    descriptor.ContentRoots = { "/work/tea/assets" };
    return descriptor;
}

const PropertyNode* Find(const std::vector<PropertyNode>& nodes, std::string_view key)
{
    for (const PropertyNode& node : nodes)
    {
        if (node.Key == key)
            return &node;
        if (const PropertyNode* child = Find(node.Children, key))
            return child;
    }
    return nullptr;
}

PropertyEditResult Commit(ProjectSettings& settings, std::string_view key, std::string_view value)
{
    return settings.BeginEdit(key) ? settings.CommitEdit(key, value) : PropertyEditResult::Refused;
}
} // namespace

TEST(ProjectSettings, AChangeIsSavedAtOnce)
{
    std::vector<ProjectDescriptor> saved;
    ProjectSettings settings([&](const ProjectDescriptor& descriptor, std::string&) {
        saved.push_back(descriptor);
        return true;
    });
    settings.Show(Descriptor());
    EXPECT_EQ(Commit(settings, "name", "Matcha"), PropertyEditResult::Applied);
    ASSERT_EQ(saved.size(), 1u);
    EXPECT_EQ(saved.back().Name, "Matcha");
    EXPECT_EQ(Find(settings.Nodes(), "name")->Value, "Matcha");
}

TEST(ProjectSettings, ARelativeRootIsTakenFromTheProjectAndARootCanBeRemoved)
{
    ProjectSettings settings([](const ProjectDescriptor&, std::string&) { return true; });
    settings.Show(Descriptor());
    EXPECT_EQ(Commit(settings, "root.add", "shared/../more"), PropertyEditResult::Applied);
    ASSERT_EQ(settings.Shown()->ContentRoots.size(), 2u);
    EXPECT_EQ(settings.Shown()->ContentRoots[1], "/work/tea/more");
    EXPECT_EQ(settings.Act("root:0", "remove", {}), PropertyEditResult::Applied);
    ASSERT_EQ(settings.Shown()->ContentRoots.size(), 1u);
    EXPECT_EQ(settings.Shown()->ContentRoots[0], "/work/tea/more");
}

TEST(ProjectSettings, AFailedSaveKeepsWhatWasThereAndSaysWhy)
{
    ProjectSettings settings([](const ProjectDescriptor&, std::string& error) {
        error = "read-only file";
        return false;
    });
    settings.Show(Descriptor());
    EXPECT_EQ(Commit(settings, "module", "/elsewhere/game.so"), PropertyEditResult::Refused);
    EXPECT_EQ(settings.Shown()->GameModulePath, "/work/tea/build/game.so");
    const PropertyNode* problem = Find(settings.Nodes(), "problem");
    ASSERT_NE(problem, nullptr);
    EXPECT_EQ(problem->Value, "read-only file");
}

TEST(ProjectSettings, AnUnreadableProjectSaysWhyAndOffersNothingToEdit)
{
    ProjectSettings settings([](const ProjectDescriptor&, std::string&) { return true; });
    settings.Show(std::nullopt, "not a project file");
    const std::vector<PropertyNode> nodes = settings.Nodes();
    ASSERT_EQ(nodes.size(), 1u);
    EXPECT_EQ(nodes[0].Value, "not a project file");
    EXPECT_FALSE(settings.BeginEdit("name"));
}
