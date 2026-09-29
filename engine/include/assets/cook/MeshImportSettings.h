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

// A mesh source's import sidecar ("hero.blend.meta"), dev-only under
// SENCHA_ENABLE_COOK. Schema and rationale: docs/assets/pipeline.md, "Import
// sidecar".

struct MeshClipSettings
{
    // In authored order.
    std::vector<AnimationClipEvent> Events;
    bool ExtractRootMotion = false;

    [[nodiscard]] bool IsDefault() const { return Events.empty() && !ExtractRootMotion; }
};

struct MeshImportSettings
{
    // By clip name.
    std::map<std::string, MeshClipSettings> Clips;
};

// The fragment a mesh source's clip artifact is named by: "asset://<source>#anim:<clip>".
inline constexpr std::string_view kMeshClipFragmentPrefix = "anim:";

// Where a cooked clip's events are authored. Empty for a path that is not a
// mesh source's clip.
struct MeshClipSource
{
    std::string SourceRelPath;
    std::string ClipName;
};
[[nodiscard]] std::optional<MeshClipSource> MeshClipSourceOf(std::string_view clipPath);

// Empty input yields the defaults. Unknown fields fail like malformed JSON, so a
// typo cannot cook a clip that silently lost its events.
[[nodiscard]] bool ParseMeshImportSettings(std::span<const std::byte> bytes,
                                           MeshImportSettings& out,
                                           std::string* error = nullptr);

// Round-trips through ParseMeshImportSettings unchanged.
[[nodiscard]] std::string WriteMeshImportSettings(const MeshImportSettings& settings);
