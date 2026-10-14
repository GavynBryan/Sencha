#include "project/AssetKinds.h"
#include "project/AssetQuery.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Finding content by typing: a name that begins with what was typed ranks
// first, a word inside a name next, anywhere in a name or folder after; and
// `t:` and `in:` narrow to a kind or a folder the way the browser's own
// controls do.
namespace
{
ProjectFile File(std::string path, std::string extension, std::string subtype = {})
{
    return { .AssetPath = std::move(path), .Extension = std::move(extension), .Subtype = std::move(subtype) };
}

std::vector<ProjectFile> Content()
{
    return { File("asset://materials/dev/brick_wall.smat", ".smat"),
             File("asset://materials/dev/glass.smat", ".smat"),
             File("asset://materials/stone/wall_brick.smat", ".smat"),
             File("asset://textures/brick.png", ".png"),
             File("asset://data/brickyard.sdata", ".sdata", "movement.profile"),
             File("asset://levels/hub.sworld", ".sworld") };
}

std::vector<std::string> Paths(const std::vector<const ProjectFile*>& found)
{
    std::vector<std::string> paths;
    for (const ProjectFile* file : found)
        paths.push_back(file->AssetPath);
    return paths;
}
} // namespace

TEST(AssetQuery, ANameBeginningWithWhatWasTypedRanksFirst)
{
    const std::vector<ProjectFile> files = Content();
    EXPECT_EQ(Paths(FindAssets(files, { .Text = "brick" })),
              (std::vector<std::string>{ "asset://textures/brick.png", "asset://materials/dev/brick_wall.smat",
                                         "asset://data/brickyard.sdata", "asset://materials/stone/wall_brick.smat" }));
    EXPECT_EQ(Paths(FindAssets(files, { .Text = "dev" })),
              (std::vector<std::string>{ "asset://materials/dev/brick_wall.smat", "asset://materials/dev/glass.smat" }))
        << "a word found only in the folder still finds";
    EXPECT_EQ(FindAssets(files, { .Text = "brick glass" }).size(), 0u) << "every word has to be there";
}

TEST(AssetQuery, KindAndFolderNarrowFromTheTextOrTheControls)
{
    const std::vector<ProjectFile> files = Content();
    EXPECT_EQ(FindAssets(files, { .Text = "t:material" }).size(), 3u);
    EXPECT_EQ(Paths(FindAssets(files, { .Text = "brick t:movement" })),
              (std::vector<std::string>{ "asset://data/brickyard.sdata" }))
        << "a data asset's kind is its subtype";
    EXPECT_EQ(FindAssets(files, { .Text = "brick in:stone" }).size(), 1u);
    EXPECT_EQ(FindAssets(files, { .Category = "Materials" }).size(), 3u);
    EXPECT_EQ(FindAssets(files, { .Folder = "materials" }).size(), 3u) << "a folder holds the folders under it";
    EXPECT_EQ(FindAssets(files, { .Folder = "materials/dev" }).size(), 2u);
}

TEST(AssetQuery, ARecentListKeepsItsOrderWhenNothingIsTyped)
{
    const std::vector<ProjectFile> recent = { File("asset://levels/hub.sworld", ".sworld"),
                                              File("asset://materials/dev/glass.smat", ".smat") };
    EXPECT_EQ(Paths(FindAssets(recent, { .KeepOrder = true })),
              (std::vector<std::string>{ "asset://levels/hub.sworld", "asset://materials/dev/glass.smat" }));
    EXPECT_EQ(Paths(FindAssets(recent, {})).front(), "asset://materials/dev/glass.smat") << "otherwise by name";
}

TEST(AssetQuery, EveryFolderAndTheFoldersAboveIt)
{
    EXPECT_EQ(FoldersOf(Content()), (std::vector<std::string>{ "data", "levels", "materials", "materials/dev",
                                                               "materials/stone", "textures" }));
    EXPECT_EQ(FolderOf(Content()[0]), "materials/dev");
    EXPECT_EQ(NameOf(Content()[0]), "brick_wall.smat");
}

TEST(AssetKinds, AFileIsCalledByItsKindAndADataAssetByItsSubtype)
{
    EXPECT_EQ(KindOf(File("asset://a.smat", ".smat")).Label, "Material");
    EXPECT_EQ(KindOf(File("asset://a.smat", ".smat")).Category, "Materials");
    EXPECT_EQ(KindOf(File("asset://a.sdata", ".sdata", "movement.profile")).Label, "Movement Profile");
    EXPECT_EQ(KindOf(File("asset://a.sdata", ".sdata", "animation.slot_map")).Label, "Animation Slot Map");
    EXPECT_EQ(KindOf(File("asset://a.xyz", ".xyz")).Category, "Other");
    for (const ProjectFile& file : Content())
        EXPECT_NE(std::ranges::find(AssetCategories(), KindOf(file).Category), AssetCategories().end());
}
