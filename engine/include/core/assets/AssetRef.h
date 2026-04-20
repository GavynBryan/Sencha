#pragma once

#include <cstdint>
#include <string>
#include <string_view>

//=============================================================================
// AssetType
//
// Stable tag identifying what kind of asset an AssetRef points at.
// Serialized as a string ("Mesh", "Material", ...) in text formats.
//=============================================================================
enum class AssetType : uint16_t
{
    Unknown  = 0,
    Mesh     = 1,
    Material = 2,
    Texture  = 3,
    Scene    = 4,
    Geometry = 5,
    Audio    = 6,
    Script   = 7,
};

enum class AssetSourceKind : uint16_t
{
    Unknown = 0,
    File,
    Procedural,
    Generated,
    Embedded,
};

inline std::string_view AssetTypeToString(AssetType type)
{
    switch (type)
    {
    case AssetType::Mesh:     return "Mesh";
    case AssetType::Material: return "Material";
    case AssetType::Texture:  return "Texture";
    case AssetType::Scene:    return "Scene";
    case AssetType::Geometry: return "Geometry";
    case AssetType::Audio:    return "Audio";
    case AssetType::Script:   return "Script";
    default:                  return "Unknown";
    }
}

inline bool AssetTypeFromString(std::string_view name, AssetType& out)
{
    if (name == "Mesh")     { out = AssetType::Mesh;     return true; }
    if (name == "Material") { out = AssetType::Material; return true; }
    if (name == "Texture")  { out = AssetType::Texture;  return true; }
    if (name == "Scene")    { out = AssetType::Scene;    return true; }
    if (name == "Geometry") { out = AssetType::Geometry; return true; }
    if (name == "Audio")    { out = AssetType::Audio;    return true; }
    if (name == "Script")   { out = AssetType::Script;   return true; }
    return false;
}

inline std::string_view AssetSourceKindToString(AssetSourceKind kind)
{
    switch (kind)
    {
    case AssetSourceKind::File:       return "File";
    case AssetSourceKind::Procedural: return "Procedural";
    case AssetSourceKind::Generated:  return "Generated";
    case AssetSourceKind::Embedded:   return "Embedded";
    default:                          return "Unknown";
    }
}

//=============================================================================
// AssetRef
//
// Stable, serializable reference to an asset. Used in scene files and editor
// data; never stored in runtime components (those use cache handles).
//
// Fields:
//   Type  - expected asset type; validated on resolve.
//   Path  - virtual asset path, e.g. "asset://meshes/dev/cube.smesh".
//           Used as the primary identity until AssetId is introduced.
//=============================================================================
struct AssetRef
{
    AssetType   Type = AssetType::Unknown;
    std::string Path;

    bool IsValid() const
    {
        return Type != AssetType::Unknown && !Path.empty();
    }
};
