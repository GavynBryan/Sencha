#pragma once

#include "project/ProjectFiles.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

// What a browser was asked for. Text is what was typed: words each of which a
// file's name or path must hold, and `t:kind` and `in:folder` narrowing it to
// a kind (a category or a label: `t:material`, `t:movement`) or a folder.
// Category and Folder narrow it the same way from the browser's own controls.
struct AssetQuery
{
    std::string Text = {};
    std::string Category = {};
    // A folder as FolderOf names it; empty for every folder.
    std::string Folder = {};
    // Files that rank alike keep the order they were given in -- a list of
    // the most recent -- rather than going by name.
    bool KeepOrder = false;

    bool operator==(const AssetQuery&) const = default;
};

// The files `query` finds, best first: a name that begins with what was typed,
// then a name where a word does, then a name or path holding it anywhere;
// alike, by name and then path.
[[nodiscard]] std::vector<const ProjectFile*> FindAssets(std::span<const ProjectFile> files, const AssetQuery& query);

// The folder a file is in, as its asset path names it without the scheme:
// "materials/dev" for asset://materials/dev/glass.smat; empty at the root.
[[nodiscard]] std::string FolderOf(const ProjectFile& file);
// A file's name, without its folder.
[[nodiscard]] std::string_view NameOf(const ProjectFile& file);

// Every folder holding a file, and every folder above one, sorted.
[[nodiscard]] std::vector<std::string> FoldersOf(std::span<const ProjectFile> files);
