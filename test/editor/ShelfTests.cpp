#include "shell/Shelf.h"

#include <gtest/gtest.h>

#include <SDL3/SDL_keycode.h>

#include <set>
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

// The search field has the keys while the shelf is up; what it does not edit
// with still reaches the window's actions, the shelf's own verbs among them.
TEST(SearchFieldKeeps, WhatItEditsWithAndNothingElse)
{
    EXPECT_TRUE(SearchFieldKeeps({ SDLK_D, {} }, true)) << "a letter is typed";
    EXPECT_TRUE(SearchFieldKeeps({ SDLK_V, { .Ctrl = true } }, false)) << "pasted";
    EXPECT_TRUE(SearchFieldKeeps({ SDLK_DELETE, {} }, false)) << "a character is deleted";
    EXPECT_FALSE(SearchFieldKeeps({ SDLK_DELETE, {} }, true)) << "nothing to delete: the chosen file's";
    EXPECT_FALSE(SearchFieldKeeps({ SDLK_D, { .Ctrl = true } }, false));
    EXPECT_FALSE(SearchFieldKeeps({ SDLK_Z, { .Ctrl = true } }, false)) << "the field has no undo of its own";
    EXPECT_FALSE(SearchFieldKeeps({ SDLK_F2, {} }, false));
    EXPECT_FALSE(SearchFieldKeeps({ SDLK_1, { .Alt = true } }, false));
}

TEST(ShelfFileNames, ARenameKeepsTheKindAndASlashMovesIt)
{
    EXPECT_EQ(RenamedPath("asset://materials/stone.smat", "materials/rock"), "asset://materials/rock.smat");
    EXPECT_EQ(RenamedPath("asset://materials/stone.smat", " walls/stone/ "), "asset://walls/stone.smat");
    EXPECT_EQ(RenamedPath("asset://materials/stone.smat", " / "), "");
}
