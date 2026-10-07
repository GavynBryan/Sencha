#include "shell/Shelf.h"

#include <gtest/gtest.h>

#include <string>

// What the shelf offers when raised to choose a file for something: the kinds
// the chooser accepts, or exactly the files it names, and never one it
// withholds by asset path or by path on disk.
namespace
{
ProjectFile File(std::string assetPath, std::string extension, std::string subtype = {})
{
    ProjectFile file;
    file.FilePath = std::filesystem::path("/project/content") / assetPath.substr(std::string("asset://").size());
    file.AssetPath = std::move(assetPath);
    file.Extension = std::move(extension);
    file.Subtype = std::move(subtype);
    return file;
}
} // namespace

TEST(ShelfPick, OffersTheKindsItAccepts)
{
    const ShelfPick pick{ .Title = "Rig", .Accepts = { { ".smat" }, { ".sdata", "animation.rig" } } };
    EXPECT_TRUE(pick.Offers(File("asset://materials/stone.smat", ".smat")));
    EXPECT_TRUE(pick.Offers(File("asset://rigs/hero.sdata", ".sdata", "animation.rig")));
    EXPECT_FALSE(pick.Offers(File("asset://input/move.sdata", ".sdata", "input.profile")));
    EXPECT_FALSE(pick.Offers(File("asset://levels/hub.sscene", ".sscene")));
}

TEST(ShelfPick, NamedFilesAreExactlyWhatItOffers)
{
    const ShelfPick pick{ .Title = "Clip", .Accepts = { { ".smat" } }, .Only = { "asset://clips/walk.sanim" } };
    EXPECT_TRUE(pick.Offers(File("asset://clips/walk.sanim", ".sanim")));
    EXPECT_FALSE(pick.Offers(File("asset://materials/stone.smat", ".smat")));
}

TEST(ShelfPick, AWithheldFileIsNotOfferedByEitherName)
{
    const ShelfPick byAsset{ .Title = "Prefab", .Accepts = { { ".sscene" } }, .Withheld = { "asset://levels/hub.sscene" } };
    const ShelfPick byDisk{ .Title = "Prefab", .Accepts = { { ".sscene" } }, .Withheld = { "/project/content/levels/hub.sscene" } };
    const ProjectFile hub = File("asset://levels/hub.sscene", ".sscene");
    EXPECT_FALSE(byAsset.Offers(hub));
    EXPECT_FALSE(byDisk.Offers(hub));
    EXPECT_TRUE(byDisk.Offers(File("asset://props/crate.sscene", ".sscene")));
}
