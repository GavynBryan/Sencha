#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/Handle.h>
#include <core/handle/Owned.h>
#include <core/logging/LoggingProvider.h>
#include <script/ScriptModule.h>

#include <cstdint>
#include <memory>
#include <string>

//=============================================================================
// ScriptCache (docs/plans/t-language-v1-spec.md, section E milestone 4)
//
// Path-keyed, ref-counted cache of validated T bytecode modules. CPU-only,
// like AudioClipCache: no GPU half. The cache holds the immutable parsed +
// validated module; per-World linking (tag ids, ComponentId + offsets, host
// slots) happens later in the world-side ScriptLink step, so a module can be
// shared across worlds.
//
// Modules are stored behind a shared_ptr so a hot-reload swap can install a
// new module while VM invocations already dispatching against the old one
// finish safely (they hold the shared_ptr for the duration of a callback).
//=============================================================================

using ScriptHandle = Handle<struct ScriptHandleTag>;

struct ScriptEntry
{
    std::shared_ptr<const ScriptModule> Module;
    uint64_t SchemaHash = 0; // ComputeScriptComponentSchemaHash(*Module)
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
};

class ScriptCache;
using ScriptCacheHandle = Owned<ScriptHandle>;

// Distinguishes the two hot-reload outcomes (spec answer 17).
enum class ScriptReloadResult
{
    NotResident,   // path not loaded; nothing swapped
    Swapped,       // same component layout, module replaced in place
    LayoutChanged, // component schema differs; caller must reload the world
};

class ScriptCache : public AssetCache<ScriptCache, ScriptHandle, ScriptEntry>
{
public:
    explicit ScriptCache(LoggingProvider& logging);
    ~ScriptCache() override;

    ScriptCache(const ScriptCache&) = delete;
    ScriptCache& operator=(const ScriptCache&) = delete;
    ScriptCache(ScriptCache&&) = delete;
    ScriptCache& operator=(ScriptCache&&) = delete;

    // Registers a validated module under `path` (refcount 1, caller owns).
    // First registration wins the dedup contract; a duplicate path gains a
    // reference and `module` is discarded.
    [[nodiscard]] ScriptHandle Register(std::string_view path,
                                        std::shared_ptr<const ScriptModule> module);

    [[nodiscard]] ScriptHandle Find(std::string_view path) const;

    [[nodiscard]] std::string_view GetName(ScriptHandle handle) const;

    // The shared module for a handle, or nullptr if the handle is stale.
    [[nodiscard]] std::shared_ptr<const ScriptModule> Get(ScriptHandle handle) const;

    // Hot-reload swap. Same component schema hash: replace the module in the
    // existing slot (handle, generation, refcount untouched) and return
    // Swapped. Different hash: leave the slot alone and return LayoutChanged
    // so the caller routes to a world reload. NotResident if the path is not
    // loaded.
    [[nodiscard]] ScriptReloadResult ReloadInPlace(std::string_view path,
                                                   std::shared_ptr<const ScriptModule> module);

private:
    friend class AssetCache<ScriptCache, ScriptHandle, ScriptEntry>;

    // AssetCache CRTP hooks. OnLoad is unused (modules enter via Register,
    // like AudioClipCache), so it always fails.
    bool OnLoad(std::string_view path, ScriptEntry& out);
    void OnFree(ScriptEntry& entry);
    bool IsEntryLive(const ScriptEntry& entry) const;

    Logger& Log;
};
