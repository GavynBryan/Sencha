#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/Handle.h>
#include <core/handle/Owned.h>
#include <core/logging/LoggingProvider.h>

#include <cstdint>
#include <memory>
#include <string>

#include <world/scene/SmapFormat.h>

//=============================================================================
// SceneHandle
//
// Opaque generational handle returned by SceneCache. One of the engine's
// unified Handle<Tag> types.
//=============================================================================
using SceneHandle = Handle<struct SceneHandleTag>;

//=============================================================================
// SceneEntry
//
// Internal slot type used by AssetCache. A resident scene is its parsed,
// validated .smap contents -- immutable CPU data held behind a shared_ptr so
// the payload address is stable while slots relocate, and so a consumer that
// captured it (a zone build on a task thread) outlives even a release.
//=============================================================================
struct SceneEntry
{
    std::shared_ptr<const SmapContents> Contents;
    uint32_t    Generation = 0;
    uint32_t    RefCount   = 0;
    std::string PathKey;
};

class SceneCache;
using SceneCacheHandle = Owned<SceneHandle>;

//=============================================================================
// SceneCache
//
// Path-keyed, ref-counted cache of parsed cooked scenes. The cache performs
// no file IO (Decision I -- loaders receive bytes): validated contents enter
// through Register(), the commit half of SceneAssetLoader. Residency means
// spawning the same scene many times parses it once; entities are built per
// spawn from the shared contents, never mutated in place.
//=============================================================================
class SceneCache : public AssetCache<SceneCache, SceneHandle, SceneEntry, AssetType::Scene>
{
public:
    explicit SceneCache(LoggingProvider& logging);
    ~SceneCache() override;

    SceneCache(const SceneCache&) = delete;
    SceneCache& operator=(const SceneCache&) = delete;
    SceneCache(SceneCache&&) = delete;
    SceneCache& operator=(SceneCache&&) = delete;

    // Registers parsed contents under `path` (refcount 1, owned by the
    // caller). If `path` is already registered, the existing entry gains a
    // reference and `contents` is discarded -- first registration wins, the
    // dedup contract every cache shares.
    [[nodiscard]] SceneHandle Register(std::string_view path, SmapContents contents);

    // Resolves a registered path without taking a reference. Invalid handle
    // if the path is unknown.
    [[nodiscard]] SceneHandle Find(std::string_view path) const;

    [[nodiscard]] std::string_view GetName(SceneHandle handle) const;

    // Returns nullptr if the handle is invalid or has been released. The
    // contents are shared and immutable; they stay valid while the caller
    // holds a reference.
    [[nodiscard]] const SmapContents* Get(SceneHandle handle) const;

    // The shared payload itself, for consumers that read it beyond the drain
    // callback that resolved the handle (a task-thread zone build). Safe to
    // hold across releases; null if the handle is invalid or released.
    [[nodiscard]] std::shared_ptr<const SmapContents> GetShared(SceneHandle handle) const;

private:
    friend class AssetCache<SceneCache, SceneHandle, SceneEntry, AssetType::Scene>;

    // AssetCache CRTP hooks.
    void OnFree(SceneEntry& entry);
    bool IsEntryLive(const SceneEntry& entry) const;

    Logger& Log;
};
