#pragma once

#include "scene_source/SceneComposition.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

//=============================================================================
// SceneSourceCache
//
// The editor's side of the source lookup seam: resolves asset://...sscene
// references against the project's content roots, parsing each file once and
// re-reading it when its timestamp or size moves. Dev-only by construction --
// it lives in the editor layer and scans authored files; the runtime never
// sees an unexpanded instance and has no counterpart.
//
// A file that fails to parse resolves to nothing and remembers the error, so
// a caller can surface why a placement is empty instead of just that it is.
//=============================================================================
class SceneSourceCache : public ISceneSourceLookup
{
public:
    explicit SceneSourceCache(std::vector<std::filesystem::path> roots)
        : Roots(std::move(roots))
    {
    }

    const SceneSourceDocument* Find(std::string_view assetPath) override;

    // The parse or resolution error of the most recent failed Find, empty when
    // it succeeded. One slot: the caller reports failures as it hits them.
    [[nodiscard]] const std::string& LastError() const { return LastError_; }

    // Whether a resolve through this cache ever loaded `assetPath` --
    // directly or as a nested source. This IS the dependency question: a
    // document depends on exactly what its cache had to read.
    [[nodiscard]] bool HasLoaded(std::string_view assetPath) const
    {
        return Entries.contains(std::string(assetPath));
    }

    // Drops one entry so the next Find re-reads the file. The explicit form
    // of the timestamp invalidation, for the saver who KNOWS the file moved:
    // same-second rewrites can leave mtime and size both unchanged.
    void Invalidate(std::string_view assetPath)
    {
        Entries.erase(std::string(assetPath));
    }

private:
    struct Entry
    {
        SceneSourceDocument Document;
        std::filesystem::file_time_type WriteTime{};
        std::uintmax_t Size = 0;
        bool Valid = false;
    };

    std::vector<std::filesystem::path> Roots;
    std::unordered_map<std::string, Entry> Entries; // by virtual asset path
    std::string LastError_;
};
