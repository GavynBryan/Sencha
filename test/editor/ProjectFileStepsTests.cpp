#include "documents/DocumentSourceSet.h"
#include "project/ProjectContentMount.h"
#include "project/ProjectFileSteps.h"

#include <assets/cook/ContentImporters.h>
#include <assets/cook/CookedCache.h>
#include <assets/cook/ImportOnDemand.h>
#include <assets/runtime/ContentMount.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetIdentity.h>
#include <core/assets/AssetRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <world/serialization/ComponentSerializerRegistry.h>

#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <random>
#include <string>
#include <vector>

// A project's files moved, copied and trashed as steps in the one journal: a
// move takes the file's sidecar and id with it and names it anew wherever it
// was named, and Ctrl+Z puts every byte back.
namespace
{
const AssetId kId{ 0x00000000000000a1ull };

class Project
{
public:
    Project()
        : Root(std::filesystem::temp_directory_path() / ("sencha_file_steps_" + std::to_string(std::random_device{}())))
    {
    }
    ~Project()
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
    bool Exists(const std::string& relative) const { return std::filesystem::exists(Root / relative); }

    std::filesystem::path Root;
};

// A document source holding some of the project's files open; a level takes
// a rewrite of what it names as read, everything else keeps names as read.
class OpenFiles final : public DocumentSource
{
public:
    explicit OpenFiles(DocumentSourceSet& journal) : Journal(journal) { Journal.AddSource(*this); }
    ~OpenFiles() override { Journal.RemoveSource(*this); }

    void AppendChangedDocuments(std::vector<DocumentRef>&) override {}
    [[nodiscard]] DocumentSaveResult SaveDocument(std::string_view) override { return {}; }
    [[nodiscard]] bool SettleDocument(std::string_view, ConflictChoice, std::string&) override { return true; }
    void StepDocument(std::string_view, DocumentStep) override {}
    void CancelDocumentEdits() override {}
    void DiscardDocument(std::string_view) override {}
    [[nodiscard]] bool HoldsFile(const std::filesystem::path& file) const override
    {
        return Held.contains(file.filename().string());
    }
    [[nodiscard]] bool KeepsNamesAsRead(const std::filesystem::path& file) const override
    {
        return HoldsFile(file) && !file.filename().string().ends_with(".sscene");
    }
    void NamesRewritten(const std::filesystem::path& file) override { Rewritten.push_back(file.filename().string()); }

    DocumentSourceSet& Journal;
    std::set<std::string> Held;
    std::vector<std::string> Rewritten;
};

struct Fixture
{
    explicit Fixture(const Project& project)
        : Open(Journal)
        , Steps({ project.Root }, Journal,
                { .IsAsset = [](const ProjectFile& file) { return file.Extension != ".txt"; },
                  .Changed =
                      [this](const ProjectFileChange& change) {
                          Changes.push_back((change.What == ProjectFileChange::Kind::Gone ? "-" : "+")
                                            + change.AssetPath);
                      } })
    {
    }

    DocumentSourceSet Journal;
    OpenFiles Open;
    std::vector<std::string> Changes;
    ProjectFileSteps Steps;
};
} // namespace

TEST(NamingMovedAsset, ABarePathIsNamedByIdAndAHintFollows)
{
    const std::string text = R"({
  // was "asset://m/old.smat" once
  "base": "asset://m/old.smat",
  "held": { "id": "00000000000000a1", "path": "asset://m/old.smat" },
  "other": "asset://m/older.smat"
})";
    const std::optional<std::string> named = NamingMovedAsset(text, "asset://m/old.smat", "asset://m/new.smat", kId);
    ASSERT_TRUE(named.has_value());
    EXPECT_EQ(*named, R"({
  // was "asset://m/old.smat" once
  "base": { "id": "00000000000000a1", "path": "asset://m/new.smat" },
  "held": { "id": "00000000000000a1", "path": "asset://m/new.smat" },
  "other": "asset://m/older.smat"
})");
    EXPECT_FALSE(NamingMovedAsset(R"({"a": "asset://x.smat"})", "asset://m/old.smat", "asset://m/new.smat", kId).has_value());
}

TEST(NamingMovedAsset, PartsAndACookedSceneMoveWithTheirSource)
{
    const std::optional<std::string> part =
        NamingMovedAsset(R"({ clip: 'asset://m/hero.blend#anim:Walk' })", "asset://m/hero.blend", "asset://n/hero.blend", kId);
    ASSERT_TRUE(part.has_value());
    EXPECT_EQ(*part, std::string(R"({ clip: { "id": ")") + AssetIdToString(AssetIdOfPart(kId, "anim:Walk"))
                         + R"(", "path": "asset://n/hero.blend#anim:Walk" } })");

    const std::optional<std::string> scene =
        NamingMovedAsset(R"({"pawn": "asset://p/pawn.smap"})", "asset://p/pawn.sscene", "asset://q/pawn.sscene", kId);
    ASSERT_TRUE(scene.has_value());
    EXPECT_EQ(*scene, R"({"pawn": { "id": "00000000000000a1", "path": "asset://q/pawn.smap" }})");
}

TEST(ProjectFileSteps, AMoveTakesTheIdAndNamesItAnewAndUndoPutsEveryByteBack)
{
    Project project;
    const std::string level = "{ format_version: 1, settings: { default_material: \"asset://m/stone.smat\" } }";
    const std::string material = "{\n  \"version\": 2\n}\n";
    project.Write("levels/hub.sscene", level);
    project.Write("m/stone.smat", material);
    Fixture files(project);

    ASSERT_EQ(files.Steps.Move("asset://m/stone.smat", "asset://m/rock/stone.smat"), "");
    EXPECT_FALSE(project.Exists("m/stone.smat"));
    EXPECT_EQ(ReadStatedAssetId(project.Root / "m/rock/stone.smat"), AssetIdOfPath("asset://m/stone.smat"))
        << "it keeps the id it was born with";
    EXPECT_NE(project.Read("levels/hub.sscene").find("asset://m/rock/stone.smat"), std::string::npos);
    EXPECT_EQ(files.Changes, (std::vector<std::string>{ "-asset://m/stone.smat", "+asset://m/rock/stone.smat" }));

    files.Journal.Undo();
    EXPECT_EQ(project.Read("m/stone.smat"), material);
    EXPECT_EQ(project.Read("levels/hub.sscene"), level);
    EXPECT_FALSE(project.Exists("m/rock/stone.smat"));

    files.Journal.Redo();
    EXPECT_TRUE(project.Exists("m/rock/stone.smat"));
    EXPECT_NE(project.Read("levels/hub.sscene").find("asset://m/rock/stone.smat"), std::string::npos);
}

TEST(ProjectFileSteps, AFileMovesWithItsSidecarAndItsSettings)
{
    Project project;
    project.Write("t/grit.png", "png");
    project.Write("t/grit.png.meta", "{\n  \"version\": 1,\n  \"mips\": false\n}\n");
    Fixture files(project);

    ASSERT_EQ(files.Steps.Move("asset://t/grit.png", "asset://t/ground/grit.png"), "");
    EXPECT_FALSE(project.Exists("t/grit.png.meta"));
    EXPECT_NE(project.Read("t/ground/grit.png.meta").find("\"mips\": false"), std::string::npos);
    EXPECT_EQ(ReadStatedAssetId(project.Root / "t/ground/grit.png"), AssetIdOfPath("asset://t/grit.png"));

    files.Journal.Undo();
    EXPECT_EQ(project.Read("t/grit.png.meta"), "{\n  \"version\": 1,\n  \"mips\": false\n}\n");
}

TEST(ProjectFileSteps, ACopyIsANewAssetAndUndoTakesItAway)
{
    Project project;
    const AssetId stone = AssetIdOfPath("asset://m/stone.smat");
    project.Write("m/stone.smat", "{\n  \"id\": \"" + AssetIdToString(stone) + "\",\n  \"version\": 2\n}\n");
    Fixture files(project);

    ASSERT_EQ(files.Steps.Duplicate("asset://m/stone.smat", "asset://m/stone copy.smat"), "");
    EXPECT_EQ(ReadStatedAssetId(project.Root / "m/stone copy.smat"), AssetIdOfPath("asset://m/stone copy.smat"));
    EXPECT_EQ(ReadStatedAssetId(project.Root / "m/stone.smat"), stone);

    files.Journal.Undo();
    EXPECT_FALSE(project.Exists("m/stone copy.smat"));
}

TEST(ProjectFileSteps, ATrashedFileLeavesTheProjectUntilUndone)
{
    Project project;
    project.Write("t/grit.png", "png");
    project.Write("t/grit.png.meta", "{ \"id\": \"00000000000000b2\" }");
    Fixture files(project);
    const std::vector<std::filesystem::path> roots{ project.Root };

    ASSERT_EQ(files.Steps.Trash("asset://t/grit.png"), "");
    EXPECT_FALSE(project.Exists("t/grit.png"));
    EXPECT_TRUE(ScanProjectFiles(roots).empty()) << "the trash is no part of the project";
    EXPECT_EQ(files.Changes, (std::vector<std::string>{ "-asset://t/grit.png" }));

    files.Journal.Undo();
    EXPECT_EQ(project.Read("t/grit.png"), "png");
    EXPECT_EQ(ReadStatedAssetId(project.Root / "t/grit.png"), AssetId{ 0xb2 });
}

TEST(ProjectFileSteps, AStepThatCannotBeTakenSaysWhy)
{
    Project project;
    project.Write("m/stone.smat", "{}");
    project.Write("m/brick.smat", "{}");
    Fixture files(project);
    files.Open.Held.insert("brick.smat");

    EXPECT_NE(files.Steps.Move("asset://m/stone.smat", "asset://m/brick.smat"), "") << "taken";
    EXPECT_NE(files.Steps.Move("asset://m/stone.smat", "asset://m/stone.png"), "") << "a kind is kept";
    EXPECT_NE(files.Steps.Trash("asset://m/brick.smat"), "") << "open in an editor";
    EXPECT_NE(files.Steps.Trash("asset://m/gone.smat"), "");
    EXPECT_FALSE(files.Journal.CanUndo());
}

TEST(ProjectFileSteps, AFileNamedByAnOpenDocumentThatKeepsNamesStaysWhereItIs)
{
    Project project;
    const std::string material = R"({ "base_color_texture": "asset://t/grit.png" })";
    project.Write("m/stone.smat", material);
    project.Write("t/grit.png", "png");
    Fixture files(project);
    files.Open.Held.insert("stone.smat");

    EXPECT_NE(files.Steps.Move("asset://t/grit.png", "asset://t/sand.png"), "");
    EXPECT_TRUE(project.Exists("t/grit.png"));
    EXPECT_EQ(project.Read("m/stone.smat"), material);
    EXPECT_FALSE(project.Exists("t/grit.png.meta")) << "nothing was stated for a step not taken";
}

TEST(ProjectFileSteps, AnOpenLevelTakesTheNamesRewrittenInItAsRead)
{
    Project project;
    project.Write("levels/hub.sscene", R"({ "m": "asset://m/stone.smat" })");
    project.Write("m/stone.smat", "{}");
    Fixture files(project);
    files.Open.Held.insert("hub.sscene");

    ASSERT_EQ(files.Steps.Move("asset://m/stone.smat", "asset://m/rock.smat"), "");
    EXPECT_NE(project.Read("levels/hub.sscene").find("asset://m/rock.smat"), std::string::npos);
    EXPECT_EQ(std::ranges::count(files.Open.Rewritten, std::string("hub.sscene")), 1);
    files.Journal.Undo();
    EXPECT_EQ(std::ranges::count(files.Open.Rewritten, std::string("hub.sscene")), 2) << "and its undoing";
}

TEST(ProjectFileSteps, AWorldsZonesGoWhereTheWorldGoes)
{
    Project project;
    project.Write("w/keep.sworld", R"({ "format_version": 4, "name": "Keep", "world_scene": "keep.world.sscene",
      "graphs": [], "zones": [ { "id": "00000000000000c1", "name": "Hall", "scene": "zones/hall.sscene",
        "bounds": { "min": [0, 0, 0], "max": [1, 1, 1] } } ] })");
    project.Write("w/keep.world.sscene", "{}");
    project.Write("w/zones/hall.sscene", "{}");
    project.Write("w/zones/loose.sscene", "{}");
    Fixture files(project);

    EXPECT_NE(files.Steps.Move("asset://w/zones/hall.sscene", "asset://w/hall.sscene"), "");
    EXPECT_NE(files.Steps.Trash("asset://w/zones/hall.sscene"), "");
    EXPECT_NE(files.Steps.Move("asset://w/keep.world.sscene", "asset://w/other.sscene"), "");
    EXPECT_NE(files.Steps.Move("asset://w/keep.sworld", "asset://elsewhere/keep.sworld"), "");
    EXPECT_FALSE(files.Journal.CanUndo());

    EXPECT_EQ(files.Steps.Move("asset://w/keep.sworld", "asset://w/castle.sworld"), "");
    EXPECT_EQ(files.Steps.Move("asset://w/zones/loose.sscene", "asset://w/loose.sscene"), "");
}

TEST(ProjectFileSteps, AFileThatIsNoAssetMovesAlone)
{
    Project project;
    project.Write("notes/todo.txt", "asset://notes/todo.txt");
    Fixture files(project);

    ASSERT_EQ(files.Steps.Move("asset://notes/todo.txt", "asset://notes/done.txt"), "");
    EXPECT_FALSE(project.Exists("notes/todo.txt.meta"));
    EXPECT_FALSE(project.Exists("notes/done.txt.meta"));
    ASSERT_EQ(files.Steps.Duplicate("asset://notes/done.txt", "asset://notes/again.txt"), "");
    EXPECT_FALSE(project.Exists("notes/again.txt.meta"));
    EXPECT_EQ(files.Steps.Version(), 2u);
}

// The asset stack follows a step as a fresh mount would have found the files:
// the record and the cooked import move with the file, and undo moves them back.
TEST(ProjectFileSteps, TheAssetStackFollowsAStep)
{
    Project project;
    project.Write("m/stone.smat", "{}");
    std::filesystem::create_directories(project.Root / "t");
    std::filesystem::copy_file(std::filesystem::path(SENCHA_REPO_ROOT) / "test/fixtures/content/assets/textures/dev/checker.png",
                               project.Root / "t/checker.png");
    LoggingProvider logging;
    ComponentSerializerRegistry serializers;
    RuntimeAssets assets(logging, serializers);
    const ContentImporterSet importers;
    const ContentRootPaths paths = ResolveContentRoot(project.Root);
    ScanContentRoot(paths, assets);
    ASSERT_TRUE(ImportAssetsOnDemand(project.Root.string(), importers.Registry(), assets.Registry, logging));
    RegisterCookedContent(paths, assets);
    ASSERT_TRUE(assets.Registry.Contains("asset://t/checker.png"));

    DocumentSourceSet journal;
    ProjectFileSteps steps({ project.Root }, journal,
                           { .Changed = [&](const ProjectFileChange& change) {
                               MountProjectFileChange(change, assets, importers.Registry(), logging);
                           } });
    const auto indexed = [&](std::string_view source) {
        CookedCacheIndex index;
        return CookedCacheIndex::LoadFromFile((paths.Cooked / kCookedCacheIndexFileName).string(), index)
               && index.Find(source) != nullptr;
    };

    ASSERT_EQ(steps.Move("asset://t/checker.png", "asset://t/ground/checker.png"), "");
    EXPECT_FALSE(assets.Registry.Contains("asset://t/checker.png"));
    ASSERT_NE(assets.Registry.FindByPath("asset://t/ground/checker.png"), nullptr);
    EXPECT_EQ(assets.Registry.FindByPath("asset://t/ground/checker.png")->Id, AssetIdOfPath("asset://t/checker.png"));
    EXPECT_FALSE(indexed("t/checker.png"));
    EXPECT_TRUE(indexed("t/ground/checker.png"));

    ASSERT_EQ(steps.Trash("asset://m/stone.smat"), "");
    EXPECT_FALSE(assets.Registry.Contains("asset://m/stone.smat"));

    journal.Undo();
    EXPECT_TRUE(assets.Registry.Contains("asset://m/stone.smat"));
    journal.Undo();
    EXPECT_FALSE(assets.Registry.Contains("asset://t/ground/checker.png"));
    ASSERT_NE(assets.Registry.FindByPath("asset://t/checker.png"), nullptr);
    EXPECT_EQ(assets.Registry.FindByPath("asset://t/checker.png")->Id, AssetIdOfPath("asset://t/checker.png"));
    EXPECT_TRUE(indexed("t/checker.png"));
}

TEST(CopyPath, TheFirstFreeNameBesideIt)
{
    const std::set<std::string> taken{ "asset://m/stone copy.smat", "asset://m/stone copy 2.smat" };
    EXPECT_EQ(CopyPath("asset://m/stone.smat", [&](const std::string& path) { return taken.contains(path); }),
              "asset://m/stone copy 3.smat");
    EXPECT_EQ(CopyPath("asset://input/move.profile.sdata", [](const std::string&) { return false; }),
              "asset://input/move copy.profile.sdata");
}

TEST(ProjectFileSteps, AFileFromOutsideArrivesAsANewAssetAndUndoTakesItAway)
{
    Project outside;
    outside.Write("Grit.PNG", "png");
    outside.Write("Grit.PNG.meta", "{ \"id\": \"00000000000000c3\", \"mips\": false }");
    Project project;
    Fixture files(project);

    ASSERT_EQ(files.Steps.Import(outside.Root / "Grit.PNG", "asset://t/Grit.PNG"), "");
    EXPECT_EQ(project.Read("t/Grit.PNG"), "png");
    EXPECT_EQ(ReadStatedAssetId(project.Root / "t/Grit.PNG"), AssetIdOfPath("asset://t/Grit.PNG"))
        << "it is new here, whatever it was elsewhere";
    EXPECT_NE(project.Read("t/Grit.PNG.meta").find("\"mips\": false"), std::string::npos);
    EXPECT_EQ(files.Changes, (std::vector<std::string>{ "+asset://t/Grit.PNG" }));
    EXPECT_NE(files.Steps.Import(outside.Root / "Grit.PNG", "asset://t/Grit.PNG"), "") << "taken";

    files.Journal.Undo();
    EXPECT_FALSE(project.Exists("t/Grit.PNG"));
    EXPECT_FALSE(project.Exists("t/Grit.PNG.meta"));
}
