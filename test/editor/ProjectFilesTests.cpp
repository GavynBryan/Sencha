#include "project/ProjectFiles.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>

// A project's content as the editor finds it: every content file under its
// roots, named by asset path, a data asset knowing its subtype, and none of
// the cooked output or the project's own bookkeeping.
namespace
{
class ContentRoot
{
public:
    ContentRoot()
    {
        std::random_device seed;
        Root = std::filesystem::temp_directory_path() / ("sencha_project_files_" + std::to_string(seed()));
        Write("materials/dev/glass.smat", "{}");
        Write("data/run.sdata", R"({ "type": "movement.profile", "version": 1, "data": {} })");
        Write("textures/Checker.PNG", "png");
        Write("textures/Checker.PNG.meta", "{}");
        Write("levels/hub.sworld.user.json", "{}");
        Write(".cooked/glass.smat", "{}");
        Write("levels/.trash/old.sscene", "{}");
    }
    ~ContentRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(Root, ignored);
    }

    std::filesystem::path Root;

    void Write(const char* relative, const char* text) const
    {
        std::filesystem::create_directories((Root / relative).parent_path());
        std::ofstream(Root / relative) << text;
    }
};
} // namespace

TEST(ProjectFiles, EveryContentFileIsFoundAndNothingElse)
{
    ContentRoot content;
    const std::filesystem::path roots[] = { content.Root };
    const std::vector<ProjectFile> files = ScanProjectFiles(roots);
    std::vector<std::string> paths;
    for (const ProjectFile& file : files)
        paths.push_back(file.AssetPath);
    EXPECT_EQ(paths, (std::vector<std::string>{ "asset://data/run.sdata", "asset://materials/dev/glass.smat",
                                                "asset://textures/Checker.PNG" }));
    EXPECT_EQ(files[0].Subtype, "movement.profile");
    EXPECT_TRUE(files[1].Subtype.empty());
    EXPECT_EQ(files[2].Extension, ".png") << "the kind is read in lower case";
    EXPECT_GT(files[0].Size, 0u);
}

TEST(ProjectFiles, OneFileIsFoundByItsAssetPath)
{
    ContentRoot content;
    const std::filesystem::path roots[] = { std::filesystem::temp_directory_path() / "nowhere", content.Root };
    const std::optional<ProjectFile> found = FindProjectFile(roots, "asset://data/run.sdata");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->Subtype, "movement.profile");
    EXPECT_EQ(found->FilePath, content.Root / "data/run.sdata");
    EXPECT_FALSE(FindProjectFile(roots, "asset://data/missing.sdata").has_value());
    EXPECT_FALSE(FindProjectFile(roots, "data/run.sdata").has_value()) << "an asset path, not a file path";
}

// A file outside the registry is found by the id it states once its hint goes
// stale; a hint that still holds keeps its path and gains the id.
TEST(ProjectFiles, AReferenceIsMadeCurrentFromTheProjectsFiles)
{
    ContentRoot content;
    content.Write("prefabs/moved/door.sscene", R"({ id: "00000000000000d0", format_version: 1 })");
    const std::vector<std::filesystem::path> roots{ content.Root };

    const AssetRef stale = CurrentInProject(roots, AssetRef{ AssetType::Scene, "asset://prefabs/door.sscene", AssetId{ 0xd0 } });
    EXPECT_EQ(stale.Path, "asset://prefabs/moved/door.sscene");

    const AssetRef bare = CurrentInProject(roots, AssetRef{ AssetType::Material, "asset://materials/dev/glass.smat" });
    EXPECT_EQ(bare.Id, AssetIdOfPath("asset://materials/dev/glass.smat")) << "it states none, so it has its path's";

    const AssetRef gone = CurrentInProject(roots, AssetRef{ AssetType::Scene, "asset://prefabs/gone.sscene", AssetId{ 0xee } });
    EXPECT_EQ(gone.Path, "asset://prefabs/gone.sscene");
    EXPECT_EQ(gone.Id, AssetId{ 0xee });
}
