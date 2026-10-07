#pragma once

#include "project/ProjectFiles.h"

class AssetImporterRegistry;
class AssetKindRegistry;

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

// One thing adopting a project's files does (docs/assets/pipeline.md, Decision A).
struct AdoptionStep
{
    enum class Kind : std::uint8_t
    {
        // The file comes to state Id, in itself or its sidecar.
        State,
        // The file stated an id another file holds; it is the copy, and states Id instead.
        Restate,
        // The sidecar left behind when File moved without it goes back beside File.
        Pair,
    };

    Kind What = Kind::State;
    ProjectFile File;
    AssetId Id{};
    std::filesystem::path Sidecar = {};
};

struct AssetAdoption
{
    std::vector<AdoptionStep> Steps;
    // Sidecars whose file is gone and which no file can be told to be.
    std::vector<std::filesystem::path> Unpaired;
};

// What leaves every file stating an id of its own:
//  - an orphaned sidecar goes back beside the one file of its name that states
//    no id and has no sidecar; with none, or several, it stays unpaired;
//  - of files stating one id, the one born with it -- the id is its path's --
//    keeps it, else the earliest modified; each other is a copy and restates;
//  - a file stating none states its path's own id.
// Every id given is free of every other; `withSidecars` names the files that
// already have one.
[[nodiscard]] AssetAdoption PlanAdoption(std::span<const ProjectFile> files,
                                         std::span<const std::pair<std::filesystem::path, AssetId>> orphans,
                                         const std::unordered_set<std::string>& withSidecars);

// Writes the plan: each statement in place, everything else in the file as it
// was; each sidecar back beside its file. Each failure is named in `errors`.
void ApplyAdoption(const AssetAdoption& adoption, std::vector<std::string>& errors);

// Whether a file is an asset -- a document stating its own id, a kind the
// runtime loads, or a source an importer cooks -- rather than a note beside one.
[[nodiscard]] bool IsAssetFile(const ProjectFile& file, const AssetKindRegistry& kinds,
                               const AssetImporterRegistry& importers);

// Plans and applies for the assets under `roots`: the files `isAsset` says are,
// where a licence or a note is not.
AssetAdoption AdoptAssetIds(std::span<const std::filesystem::path> roots,
                            const std::function<bool(const ProjectFile&)>& isAsset,
                            std::vector<std::string>& errors);
