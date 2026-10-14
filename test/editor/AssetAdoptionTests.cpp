#include "project/AssetAdoption.h"

#include <core/assets/AssetIdentity.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <vector>

// Kyusu leaves every content file stating an id of its own: new files take the
// id they were born with, a copy restates, a sidecar left behind goes back to
// its file, and running it again changes nothing.
namespace
{
ProjectFile File(std::string path, std::optional<AssetId> stated = std::nullopt, int age = 0)
{
    ProjectFile file;
    file.AssetPath = "asset://" + path;
    file.FilePath = "/project/" + path;
    file.Extension = path.substr(path.rfind('.'));
    file.StatedId = stated;
    file.Modified = std::filesystem::file_time_type{} + std::chrono::seconds(age);
    return file;
}

const AdoptionStep* StepFor(const AssetAdoption& plan, std::string_view assetPath)
{
    const auto found = std::ranges::find(plan.Steps, assetPath, [](const AdoptionStep& step) { return step.File.AssetPath; });
    return found != plan.Steps.end() ? &*found : nullptr;
}

class ProjectRoot
{
public:
    ProjectRoot()
        : Root(std::filesystem::temp_directory_path() / ("sencha_adoption_" + std::to_string(std::random_device{}())))
    {
    }
    ~ProjectRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(Root, ec);
    }
    void Write(const std::string& relative, const std::string& text) const
    {
        std::filesystem::create_directories((Root / relative).parent_path());
        std::ofstream(Root / relative, std::ios::binary) << text;
    }
    std::string Read(const std::string& relative) const
    {
        std::ifstream in(Root / relative, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }

    std::filesystem::path Root;
};
} // namespace

TEST(AssetAdoption, AFileStatingNoIdTakesTheOneItWasBornWith)
{
    const std::vector<ProjectFile> files = { File("materials/stone.smat"), File("textures/stone.png"),
                                             File("textures/taken.png", AssetIdOfPath("asset://textures/free.png")),
                                             File("textures/free.png") };
    const AssetAdoption plan = PlanAdoption(files, {}, {});
    ASSERT_EQ(plan.Steps.size(), 3u);
    EXPECT_EQ(StepFor(plan, "asset://materials/stone.smat")->Id, AssetIdOfPath("asset://materials/stone.smat"));
    EXPECT_EQ(StepFor(plan, "asset://textures/stone.png")->Id, AssetIdOfPath("asset://textures/stone.png"));
    EXPECT_NE(StepFor(plan, "asset://textures/free.png")->Id, AssetIdOfPath("asset://textures/free.png"))
        << "an id another file states is not given again";
}

TEST(AssetAdoption, OfTwoFilesStatingOneIdTheOneBornWithItKeepsIt)
{
    const AssetId id = AssetIdOfPath("asset://materials/stone.smat");
    const std::vector<ProjectFile> files = { File("materials/stone copy.smat", id, 0), File("materials/stone.smat", id, 5) };
    const AssetAdoption plan = PlanAdoption(files, {}, {});
    ASSERT_EQ(plan.Steps.size(), 1u);
    EXPECT_EQ(plan.Steps[0].What, AdoptionStep::Kind::Restate);
    EXPECT_EQ(plan.Steps[0].File.AssetPath, "asset://materials/stone copy.smat");
    EXPECT_NE(plan.Steps[0].Id, id);

    const std::vector<ProjectFile> moved = { File("a/stone.smat", AssetId{ 0x5 }, 9), File("b/stone.smat", AssetId{ 0x5 }, 3) };
    const AssetAdoption older = PlanAdoption(moved, {}, {});
    ASSERT_EQ(older.Steps.size(), 1u);
    EXPECT_EQ(older.Steps[0].File.AssetPath, "asset://a/stone.smat") << "neither was born with it: the earlier keeps it";
}

TEST(AssetAdoption, ASidecarLeftBehindGoesBackToTheOneFileOfItsName)
{
    const std::vector<ProjectFile> files = { File("textures/moved/stone.png"), File("textures/a/grit.png"),
                                             File("textures/b/grit.png") };
    const std::vector<std::pair<std::filesystem::path, AssetId>> orphans = {
        { "/project/textures/stone.png.meta", AssetId{ 0x9 } }, { "/project/textures/grit.png.meta", AssetId{ 0xa } }
    };
    const AssetAdoption plan = PlanAdoption(files, orphans, {});
    const AdoptionStep* stone = StepFor(plan, "asset://textures/moved/stone.png");
    ASSERT_NE(stone, nullptr);
    EXPECT_EQ(stone->What, AdoptionStep::Kind::Pair);
    EXPECT_EQ(stone->Id, AssetId{ 0x9 });
    EXPECT_EQ(std::ranges::count_if(plan.Steps, [](const AdoptionStep& step) {
                  return step.File.AssetPath == "asset://textures/moved/stone.png";
              }),
              1) << "its id comes back with its sidecar";
    ASSERT_EQ(plan.Unpaired.size(), 1u) << "two files of the name: nothing says which";
    EXPECT_EQ(plan.Unpaired[0], std::filesystem::path("/project/textures/grit.png.meta"));
}

TEST(AssetAdoption, AProjectEndsWithEveryFileStatingItsOwnIdAndAgainChangesNothing)
{
    ProjectRoot project;
    const std::string stoneId = AssetIdToString(AssetIdOfPath("asset://materials/stone.smat"));
    project.Write("materials/stone.smat", "{\n  \"id\": \"" + stoneId + "\",\n  \"version\": 2\n}\n");
    project.Write("materials/stone copy.smat", "{\n  \"id\": \"" + stoneId + "\",\n  \"version\": 2\n}\n");
    project.Write("materials/brick.smat", "{\n  // floor\n  \"version\": 2\n}\n");
    project.Write("textures/stone.png", "png");
    project.Write("textures/stone.png.meta", "{\n  \"version\": 1,\n  \"mips\": false\n}\n");
    project.Write("textures/grit.png", "png");
    project.Write("levels/hub.sscene", "{ format_version: 1, entities: [] }");
    project.Write("fonts/LICENSE.txt", "not an asset");
    project.Write("meshes/moved/rock.glb", "glb");
    project.Write("meshes/rock.glb.meta", "{ \"id\": \"00000000000000c7\" }");
    const std::vector<std::filesystem::path> roots{ project.Root };

    std::vector<std::string> errors;
    const auto isAsset = [](const ProjectFile& file) { return file.Extension != ".txt"; };
    const AssetAdoption first = AdoptAssetIds(roots, isAsset, errors);
    EXPECT_TRUE(errors.empty()) << errors.front();
    EXPECT_TRUE(first.Unpaired.empty());

    EXPECT_NE(project.Read("materials/brick.smat").find("// floor"), std::string::npos) << "nothing else is touched";
    EXPECT_NE(project.Read("textures/stone.png.meta").find("\"mips\": false"), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(project.Root / "meshes/moved/rock.glb.meta"));

    EXPECT_FALSE(std::filesystem::exists(project.Root / "fonts/LICENSE.txt.meta")) << "only assets are given ids";
    std::set<AssetId> ids;
    for (const ProjectFile& file : ScanProjectFiles(roots))
    {
        if (!isAsset(file))
            continue;
        ASSERT_TRUE(file.StatedId.has_value()) << file.AssetPath;
        EXPECT_TRUE(ids.insert(*file.StatedId).second) << file.AssetPath << " shares its id";
    }
    EXPECT_EQ(ReadStatedId(project.Root / "materials/stone.smat"), AssetIdOfPath("asset://materials/stone.smat"))
        << "the original keeps its id; the copy restates";
    EXPECT_EQ(ReadStatedId(project.Root / "materials/brick.smat"), AssetIdOfPath("asset://materials/brick.smat"));
    EXPECT_EQ(ReadStatedId(project.Root / "meshes/moved/rock.glb"), AssetId{ 0xc7 });

    const AssetAdoption second = AdoptAssetIds(roots, isAsset, errors);
    EXPECT_TRUE(second.Steps.empty());
}
