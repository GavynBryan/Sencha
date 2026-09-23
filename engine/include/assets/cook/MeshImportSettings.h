#pragma once

#include <anim/AnimationClip.h>
#include <assets/cook/AssetImporter.h> // kImportSettingsSuffix

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// MeshImportSettings. Dev-only, compiled under SENCHA_ENABLE_COOK.
//
// Per-source options for a mesh source (.glb, .gltf, .blend), authored as a
// JSON sidecar beside it ("hero.blend" + "hero.blend.meta"). The import driver
// hands its bytes to the importer and folds them into the cooked-cache
// freshness hash, so editing it recooks. A missing sidecar is the defaults.
//
// Clip events live here rather than in the cooked clip, because the clip is
// rebuilt from the source on every import and an event authored only into it
// would be lost. The cook copies each clip's events into its .sanim.
//
// Schema (all fields optional):
//   {
//     "version": 1,
//     "clips": {
//       "<clip>": {           // the name after "#anim:" in the clip's path
//         "events": [
//           { "key": 3, "name": "Left foot", "time": 0.25,
//             "binding": "anim.footstep",
//             "scope": "cosmetic" | "gameplay",
//             "min_weight": 0.5,
//             "inputs": { "surface": { "tag": "Surface.Grass" },
//                         "volume": { "const": 0.8 } } }
//         ]
//       }
//     }
//   }
//=============================================================================

struct MeshImportSettings
{
    // By clip name, each clip's events in authored order.
    std::map<std::string, std::vector<AnimationClipEvent>> ClipEvents;
};

// The fragment a mesh source's clip artifact is named by: "asset://<source>#anim:<clip>".
inline constexpr std::string_view kMeshClipFragmentPrefix = "anim:";

// Where a cooked clip's events are authored: the source it was cooked from,
// relative to its content root, and the name its events are keyed by in that
// source's sidecar. Empty for a path that is not a mesh source's clip.
struct MeshClipSource
{
    std::string SourceRelPath;
    std::string ClipName;
};
[[nodiscard]] std::optional<MeshClipSource> MeshClipSourceOf(std::string_view clipPath);

// Parses sidecar JSON bytes. Empty input yields the defaults; malformed JSON,
// unknown fields and invalid events fail with *error, so a typo cannot cook a
// clip that silently lost its events.
[[nodiscard]] bool ParseMeshImportSettings(std::span<const std::byte> bytes,
                                           MeshImportSettings& out,
                                           std::string* error = nullptr);

// The sidecar text for `settings`, in the schema above: what an editor saves
// and what ParseMeshImportSettings reads back unchanged.
[[nodiscard]] std::string WriteMeshImportSettings(const MeshImportSettings& settings);
