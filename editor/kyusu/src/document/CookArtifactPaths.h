#pragma once

#include <string>
#include <string_view>

// The one convention for a zone's baked-artifact paths: the relative path a
// producer stages under the cooked root, the classifier matches a prior entry
// against, and the withdrawer removes. Kept in a single place so a rename
// cannot leave a preserved output orphaned or a withdraw aimed at a stale name.
inline constexpr std::string_view kLightmapAtlasFile = "lightmap.stex";
inline constexpr std::string_view kAoAtlasFile = "ao.stex";
inline constexpr std::string_view kProbeVolumeFile = "probes.sprobe";

// Path relative to the cooked root (".cooked/") for a document's baked
// artifact. The stem is directory-qualified (levels/room_2, prefabs/turret),
// so the family lands beside the document's .smap.
[[nodiscard]] inline std::string DocumentArtifactRel(std::string_view stem,
                                                     std::string_view file)
{
    std::string rel(stem);
    rel += '/';
    rel += file;
    return rel;
}

[[nodiscard]] inline std::string LightmapAtlasRel(std::string_view stem)
{
    return DocumentArtifactRel(stem, kLightmapAtlasFile);
}

[[nodiscard]] inline std::string AoAtlasRel(std::string_view stem)
{
    return DocumentArtifactRel(stem, kAoAtlasFile);
}

[[nodiscard]] inline std::string ProbeVolumeRel(std::string_view stem)
{
    return DocumentArtifactRel(stem, kProbeVolumeFile);
}

// Whether a stored reference or artifact path ends in "/<file>": the match the
// preserve classifier uses to sort a prior entry into its output family.
[[nodiscard]] inline bool PathHasArtifactFile(std::string_view path,
                                              std::string_view file)
{
    return path.size() > file.size()
        && path[path.size() - file.size() - 1] == '/'
        && path.compare(path.size() - file.size(), file.size(), file) == 0;
}
