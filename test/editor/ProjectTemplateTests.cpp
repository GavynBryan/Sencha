#include <gtest/gtest.h>

#include "project/Project.h"

#include <filesystem>
#include <fstream>
#include <string>

//=============================================================================
// A new project as a copy of a starter template: what comes across, what stays
// behind, and what the descriptor says afterwards.
//=============================================================================
namespace
{
    void Write(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path);
        out << text;
    }

    class TemplateRoot
    {
    public:
        TemplateRoot()
            : Root(std::filesystem::temp_directory_path() / "sencha-project-template-test")
        {
            std::filesystem::remove_all(Root);
            Template = Root / "template";
            Write(Template / "project.senchaproj",
                  R"({"name":"SenchaSomeTemplate","gameModule":"build/game.so","contentRoots":["assets"]})");
            Write(Template / "CMakeLists.txt", "# a front door\n");
            Write(Template / "src" / "Game.cpp", "// game\n");
            Write(Template / "assets" / "levels" / "room.sscene", "{}");
            Write(Template / ".gitignore", "/build/\n");
            // Local state a working copy carries and a new project must not.
            Write(Template / "build" / "game.so", "binary");
            Write(Template / "assets" / ".cooked" / "levels" / "room.smap", "cooked");
            Write(Template / "assets" / "asset_ids.json", "{}");
            Write(Template / "assets" / "levels" / "room.sworld.user.json", "{}");
        }

        ~TemplateRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(Root, ec);
        }

        std::filesystem::path Root;
        std::filesystem::path Template;
    };
}

TEST(ProjectTemplate, CopiesSourcesAndContentAndRenames)
{
    const TemplateRoot fixture;
    const std::filesystem::path target = fixture.Root / "my-game";

    ProjectDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ProjectDescriptor::CreateFromTemplate(
        fixture.Template.string(), target.string(), "MyGame", descriptor, &error))
        << error;

    EXPECT_TRUE(std::filesystem::exists(target / "CMakeLists.txt"));
    EXPECT_TRUE(std::filesystem::exists(target / "src" / "Game.cpp"));
    EXPECT_TRUE(std::filesystem::exists(target / "assets" / "levels" / "room.sscene"));
    EXPECT_TRUE(std::filesystem::exists(target / ".gitignore"))
        << "the ignore list is what keeps the copy's own local state out of its repository";
    EXPECT_EQ(descriptor.Name, "MyGame");

    ProjectDescriptor reloaded;
    ASSERT_TRUE(ProjectDescriptor::Load((target / "project.senchaproj").string(), reloaded, &error))
        << error;
    EXPECT_EQ(reloaded.Name, "MyGame");
    EXPECT_EQ(std::filesystem::path(reloaded.GameModulePath).lexically_normal(),
              (target / "build" / "game.so").lexically_normal())
        << "the module path is the copy's, not the template's";
}

TEST(ProjectTemplate, LeavesLocalStateBehind)
{
    const TemplateRoot fixture;
    const std::filesystem::path target = fixture.Root / "clean";

    ProjectDescriptor descriptor;
    std::string error;
    ASSERT_TRUE(ProjectDescriptor::CreateFromTemplate(
        fixture.Template.string(), target.string(), "", descriptor, &error)) << error;

    EXPECT_FALSE(std::filesystem::exists(target / "build"));
    EXPECT_FALSE(std::filesystem::exists(target / "assets" / ".cooked"));
    EXPECT_FALSE(std::filesystem::exists(target / "assets" / "asset_ids.json"));
    EXPECT_FALSE(std::filesystem::exists(target / "assets" / "levels" / "room.sworld.user.json"));
    EXPECT_EQ(descriptor.Name, "clean") << "no name given: the directory names the project";
}

TEST(ProjectTemplate, RefusesToOverwriteAnExistingProject)
{
    const TemplateRoot fixture;
    const std::filesystem::path target = fixture.Root / "taken";
    Write(target / "project.senchaproj", "{}");

    ProjectDescriptor descriptor;
    std::string error;
    EXPECT_FALSE(ProjectDescriptor::CreateFromTemplate(
        fixture.Template.string(), target.string(), "", descriptor, &error));
    EXPECT_FALSE(error.empty());
}

TEST(ProjectTemplate, RefusesADirectoryThatIsNotATemplate)
{
    const TemplateRoot fixture;
    ProjectDescriptor descriptor;
    std::string error;
    EXPECT_FALSE(ProjectDescriptor::CreateFromTemplate(
        (fixture.Root / "nothing").string(), (fixture.Root / "out").string(), "", descriptor,
        &error));
}
