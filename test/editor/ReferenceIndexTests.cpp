#include "project/ReferenceIndex.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

// Which content files name which, read from the documents themselves: what a
// file uses, what uses it, what it names that is nowhere, and only changed
// documents read again.
namespace
{
ProjectFile File(std::string path, std::uintmax_t size = 1)
{
    ProjectFile file;
    file.AssetPath = "asset://" + path;
    file.FilePath = "/project/" + path;
    file.Extension = path.substr(path.rfind('.'));
    file.Size = size;
    return file;
}

class Disk
{
public:
    std::map<std::string, std::string> Text;
    std::vector<std::string> Read;

    ReferenceIndex::ReadText Reader()
    {
        return [this](const std::filesystem::path& path) -> std::optional<std::string> {
            Read.push_back(path.generic_string());
            const auto found = Text.find(path.generic_string());
            return found != Text.end() ? std::optional<std::string>(found->second) : std::nullopt;
        };
    }
};
} // namespace

TEST(ReferencedFile, APartIsItsFileAndACookedSceneItsSource)
{
    EXPECT_EQ(ReferencedFile("asset://meshes/hero.blend#anim:Walk"), "asset://meshes/hero.blend");
    EXPECT_EQ(ReferencedFile("asset://prefabs/pawn.smap"), "asset://prefabs/pawn.sscene");
    EXPECT_EQ(ReferencedFile("asset://materials/stone.smat"), "asset://materials/stone.smat");
}

TEST(ReferencesIn, ReadsJsonAndJson5InEitherFormAndNothingElse)
{
    const std::vector<AssetRef> json = ReferencesIn(R"({"base_color_texture": "asset://textures/stone.png"})");
    ASSERT_EQ(json.size(), 1u);
    EXPECT_EQ(json[0].Path, "asset://textures/stone.png");
    const std::vector<AssetRef> json5 =
        ReferencesIn(R"({ settings: { default_material: { id: "00000000000000a1", path: "asset://materials/stone.smat" } } })");
    ASSERT_EQ(json5.size(), 1u);
    EXPECT_EQ(json5[0].Id, AssetId{ 0xa1 });
    EXPECT_EQ(json5[0].Path, "asset://materials/stone.smat");
    EXPECT_TRUE(ReferencesIn("not a document asset://textures/stone.png").empty());
}

// A reference by id finds its file wherever it went; its stale path is only
// what it was last known by.
TEST(ReferenceIndex, AMovedFileIsStillUsedByWhatNamesItsId)
{
    Disk disk;
    disk.Text["/project/levels/hub.sscene"] =
        R"({ settings: { default_material: { id: "00000000000000a1", path: "asset://materials/old.smat" } } })";
    ProjectFile moved = File("materials/new/stone.smat");
    moved.StatedId = AssetId{ 0xa1 };
    const std::vector<ProjectFile> files = { File("levels/hub.sscene"), moved };
    ReferenceIndex index;
    index.Refresh(files, {}, disk.Reader());

    ASSERT_EQ(index.Uses("asset://levels/hub.sscene").size(), 1u);
    EXPECT_EQ(index.Uses("asset://levels/hub.sscene")[0], "asset://materials/new/stone.smat");
    EXPECT_TRUE(index.Missing("asset://levels/hub.sscene").empty());
    EXPECT_FALSE(index.IsUnused("asset://materials/new/stone.smat"));
}

TEST(ReferenceIndex, AFileUsesWhatItNamesAndIsUsedByWhatNamesIt)
{
    Disk disk;
    disk.Text["/project/materials/stone.smat"] = R"({"base_color_texture": "asset://textures/stone.png"})";
    disk.Text["/project/levels/hub.sscene"] =
        R"({ settings: { default_material: "asset://materials/stone.smat" }, brushes: [{ material: "asset://materials/stone.smat" }] })";
    const std::vector<ProjectFile> files = { File("levels/hub.sscene"), File("materials/stone.smat"),
                                             File("textures/stone.png"), File("textures/moss.png") };
    ReferenceIndex index;
    index.Refresh(files, {}, disk.Reader());

    ASSERT_EQ(index.Uses("asset://levels/hub.sscene").size(), 1u) << "named twice, used once";
    EXPECT_EQ(index.Uses("asset://levels/hub.sscene")[0], "asset://materials/stone.smat");
    ASSERT_EQ(index.UsedBy("asset://textures/stone.png").size(), 1u);
    EXPECT_EQ(index.UsedBy("asset://textures/stone.png")[0], "asset://materials/stone.smat");
    EXPECT_TRUE(index.IsUnused("asset://textures/moss.png"));
    EXPECT_FALSE(index.IsUnused("asset://materials/stone.smat"));
    EXPECT_EQ(disk.Read.size(), 2u) << "a texture names nothing and is never read";
}

TEST(ReferenceIndex, WhatIsNamedButNowhereIsMissingUnlessItIsMountedElsewhere)
{
    Disk disk;
    disk.Text["/project/rigs/hero.sdata"] =
        R"({"type": "animation.rig", "facts": "asset://animation/engine.facts.sdata", "mask": "asset://rigs/gone.sdata"})";
    const std::vector<ProjectFile> files = { File("rigs/hero.sdata") };
    const std::vector<ProjectFile> engine = { File("animation/engine.facts.sdata") };
    ReferenceIndex index;
    index.Refresh(files, engine, disk.Reader());

    ASSERT_EQ(index.Missing("asset://rigs/hero.sdata").size(), 1u);
    EXPECT_EQ(index.Missing("asset://rigs/hero.sdata")[0], "asset://rigs/gone.sdata");
    EXPECT_EQ(index.Uses("asset://rigs/hero.sdata").size(), 1u) << "the engine's content is found, not missing";
}

TEST(ReferenceIndex, OnlyChangedDocumentsAreReadAgainAndGoneOnesForgotten)
{
    Disk disk;
    disk.Text["/project/materials/a.smat"] = R"({"base_color_texture": "asset://textures/a.png"})";
    disk.Text["/project/materials/b.smat"] = R"({"base_color_texture": "asset://textures/a.png"})";
    std::vector<ProjectFile> files = { File("materials/a.smat"), File("materials/b.smat"), File("textures/a.png") };
    ReferenceIndex index;
    index.Refresh(files, {}, disk.Reader());
    EXPECT_EQ(index.UsedBy("asset://textures/a.png").size(), 2u);

    disk.Read.clear();
    disk.Text["/project/materials/b.smat"] = R"({"base_color_texture": "asset://textures/b.png"})";
    files[1].Size = 2;
    index.Refresh(files, {}, disk.Reader());
    EXPECT_EQ(disk.Read, (std::vector<std::string>{ "/project/materials/b.smat" }));
    EXPECT_EQ(index.UsedBy("asset://textures/a.png").size(), 1u);
    EXPECT_EQ(index.Missing("asset://materials/b.smat").size(), 1u);

    files.erase(files.begin());
    index.Refresh(files, {}, disk.Reader());
    EXPECT_TRUE(index.IsUnused("asset://textures/a.png")) << "a deleted file uses nothing";
}

TEST(ReferenceIndex, AReferenceToACookedSceneIsUseOfItsSource)
{
    Disk disk;
    disk.Text["/project/game.sdata"] = R"({"type": "game.settings", "pawn": "asset://prefabs/pawn.smap"})";
    const std::vector<ProjectFile> files = { File("game.sdata"), File("prefabs/pawn.sscene") };
    disk.Text["/project/prefabs/pawn.sscene"] = "{}";
    ReferenceIndex index;
    index.Refresh(files, {}, disk.Reader());
    EXPECT_TRUE(index.Missing("asset://game.sdata").empty());
    EXPECT_FALSE(index.IsUnused("asset://prefabs/pawn.sscene"));
}
