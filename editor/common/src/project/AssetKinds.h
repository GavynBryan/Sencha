#pragma once

#include "icons/IconId.h"
#include "project/ProjectFiles.h"

#include <span>
#include <string>
#include <string_view>

// What a content file is to a person browsing for it: the shelf of kinds it
// sits on, what one of it is called, and its icon. A data asset is called by
// its subtype ("Movement Profile").
struct AssetKind
{
    std::string_view Category;
    std::string Label;
    IconId Icon = IconId::File;
};

[[nodiscard]] AssetKind KindOf(const ProjectFile& file);

// Every shelf of kinds, in the order a browser offers them.
[[nodiscard]] std::span<const std::string_view> AssetCategories();
