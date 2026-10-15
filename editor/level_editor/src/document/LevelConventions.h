#pragma once

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class DataAssetTypeRegistry;
class DataSchemaRegistry;
struct BrushMesh;
struct DataSchema;

// A project's blockout conventions: the authoring choices generated geometry
// and new pieces start from. Never the player's size or step height, which
// belong to the movement and navigation data that own them.
inline constexpr std::string_view kLevelConventionsSubtype = "level.conventions";

// The schema's defaults, in metres.
inline constexpr double kDefaultShellThickness = 0.25;
inline constexpr double kDefaultOpeningWidth = 1.2;
inline constexpr double kDefaultOpeningHeight = 2.2;
inline constexpr double kDefaultSpaceHeight = 3.0;

struct LevelConventions
{
    float ShellThickness = static_cast<float>(kDefaultShellThickness);
    float OpeningWidth = static_cast<float>(kDefaultOpeningWidth);
    float OpeningHeight = static_cast<float>(kDefaultOpeningHeight);
    float SpaceHeight = static_cast<float>(kDefaultSpaceHeight);
    std::optional<AssetRef> OpenFaceMaterial;
    std::optional<AssetRef> WallMaterial;
    std::optional<AssetRef> FloorMaterial;
    std::optional<AssetRef> CeilingMaterial;
};

[[nodiscard]] const DataSchema& LevelConventionsSchema();

// So the Data workspace authors it and the data cache compiles it.
void RegisterLevelConventionsData(DataAssetTypeRegistry& types, DataSchemaRegistry& schemas);

struct LevelConventionsLookup
{
    // The asset the values came from; none when the project has none and the
    // defaults stand.
    std::optional<std::string> AssetPath;
    // Every field, defaults filled.
    JsonValue Values;
    std::vector<std::string> Problems;
};

// The project's single level.conventions asset under `roots`. Several, or one
// that does not satisfy the schema, is a problem, and the defaults stand.
[[nodiscard]] LevelConventionsLookup FindLevelConventions(std::span<const std::filesystem::path> roots);

// The typed conventions from values with every field filled, as a lookup or
// the schema's normalizer gives them.
[[nodiscard]] LevelConventions LevelConventionsFrom(const JsonValue& values);

// Floors take the floor material, ceilings the ceiling's, the rest the walls';
// a role the conventions leave unset leaves the face as it is.
void PaintByConvention(BrushMesh& mesh, const LevelConventions& conventions);
