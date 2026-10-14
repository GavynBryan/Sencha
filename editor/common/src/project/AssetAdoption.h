#pragma once

#include "project/ProjectFiles.h"

class AssetImporterRegistry;
class AssetKindRegistry;

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

// What leaves every file stating an id of its own, by the rules in
// docs/assets/pipeline.md. Every id given is free of every other; `withSidecars`
// names the files that already have one.
[[nodiscard]] AssetAdoption PlanAdoption(std::span<const ProjectFile> files,
                                         std::span<const std::pair<std::filesystem::path, AssetId>> orphans,
                                         const std::unordered_set<std::string>& withSidecars);

// Writes the plan: each statement in place, everything else in the file as it
// was; each sidecar back beside its file. Each failure is named in `errors`.
void ApplyAdoption(const AssetAdoption& adoption, std::vector<std::string>& errors);

// `text` -- a document, or a sidecar -- with the id it states removed, written
// back as its kind is written: a scene source (`json5`) as JSON5, comments
// kept, everything else as JSON. Nullopt when it is not an object.
[[nodiscard]] std::optional<std::string> WithoutStatedId(std::string_view text, bool json5);

// Whether a file is an asset -- a document stating its own id, a kind the
// runtime loads, or a source an importer cooks -- rather than a note beside one.
[[nodiscard]] bool IsAssetFile(const ProjectFile& file, const AssetKindRegistry& kinds,
                               const AssetImporterRegistry& importers);

// Plans and applies for the assets under `roots`: the files `isAsset` says are,
// where a licence or a note is not.
AssetAdoption AdoptAssetIds(std::span<const std::filesystem::path> roots,
                            const std::function<bool(const ProjectFile&)>& isAsset,
                            std::vector<std::string>& errors);
