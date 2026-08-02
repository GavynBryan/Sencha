#pragma once

#include <cstdint>
#include <string>
#include <string_view>

//=============================================================================
// AssetType
//
// Stable tag identifying what kind of asset an AssetRef points at.
// Serialized as a string ("StaticMesh", "Material", ...) in text formats:
// scene fields, the cooked cache index, and document cook receipts. Every
// value needs a name in both directions below, or artifacts carrying it
// cannot be read back.
//=============================================================================
enum class AssetType : uint16_t
{
    Unknown = 0,
    StaticMesh = 1,
    Material = 2,
    Texture = 3,
    Scene = 4,
    Geometry = 5,
    Audio = 6,
    Script = 7,
    Skeleton = 8,
    AnimationClip = 9,
    SkinnedMesh = 10,
    Collision = 11,
    ProbeVolume = 12,
};

enum class AssetSourceKind : uint16_t
{
    Unknown = 0,
    File,
    Procedural,
};

// How a member stores its asset handle. Single is one handle (e.g. a mesh);
// List is an ordered, positionally-indexed set (e.g. StaticMeshComponent's
// per-slot materials, where index binds to a mesh section). The call site
// states this so the metadata layer never names a concrete handle type.
enum class AssetArity : uint16_t
{
    Single = 0,
    List,
};

// Listed exhaustively rather than with a default arm: a new AssetType that
// nobody names here would otherwise serialize as "Unknown" and be rejected on
// read, which fails the whole cooked index or receipt containing it. -Wswitch
// turns that into a compile-time warning instead.
inline std::string_view AssetTypeToString(AssetType type)
{
    switch (type)
    {
    case AssetType::StaticMesh: return "StaticMesh";
    case AssetType::Material: return "Material";
    case AssetType::Texture:  return "Texture";
    case AssetType::Scene:    return "Scene";
    case AssetType::Geometry: return "Geometry";
    case AssetType::Audio:    return "Audio";
    case AssetType::Script:   return "Script";
    case AssetType::Skeleton: return "Skeleton";
    case AssetType::AnimationClip: return "AnimationClip";
    case AssetType::SkinnedMesh: return "SkinnedMesh";
    case AssetType::Collision: return "Collision";
    case AssetType::ProbeVolume: return "ProbeVolume";
    case AssetType::Unknown:  break;
    }
    return "Unknown";
}

inline bool AssetTypeFromString(std::string_view name, AssetType& out)
{
    if (name == "StaticMesh") { out = AssetType::StaticMesh; return true; }
    if (name == "Material") { out = AssetType::Material; return true; }
    if (name == "Texture")  { out = AssetType::Texture;  return true; }
    if (name == "Scene")    { out = AssetType::Scene;    return true; }
    if (name == "Geometry") { out = AssetType::Geometry; return true; }
    if (name == "Audio")    { out = AssetType::Audio;    return true; }
    if (name == "Script")   { out = AssetType::Script;   return true; }
    if (name == "Skeleton") { out = AssetType::Skeleton; return true; }
    if (name == "AnimationClip") { out = AssetType::AnimationClip; return true; }
    if (name == "SkinnedMesh") { out = AssetType::SkinnedMesh; return true; }
    if (name == "Collision") { out = AssetType::Collision; return true; }
    if (name == "ProbeVolume") { out = AssetType::ProbeVolume; return true; }
    return false;
}

inline std::string_view AssetSourceKindToString(AssetSourceKind kind)
{
    switch (kind)
    {
    case AssetSourceKind::File:       return "File";
    case AssetSourceKind::Procedural: return "Procedural";
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
//           The primary identity in authored data. Cooked scenes and
//           manifests pair it with the stable AssetId (core/assets/
//           AssetId.h), which resolves first with the path as fallback.
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
