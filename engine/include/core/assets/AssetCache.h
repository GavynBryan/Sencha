#pragma once

#include <core/raii/ILifetimeOwner.h>
#include <core/raii/LifetimeHandle.h>
#include <core/service/IService.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

//=============================================================================
// AssetCache<TDerived, THandle, TEntry>
//
// CRTP base for path-keyed, ref-counted asset caches. Provides:
//   - Generational slot pool (Entries, FreeSlots)
//   - Path-based deduplication (PathLookup, ref-counting)
//   - Acquire / Release / AcquireOwned public interface
//   - ILifetimeOwner integration (Attach / Detach for RAII handles)
//
// The derived class must supply three static-dispatch hooks by implementing
// these methods (called through CRTP -- no virtual dispatch):
//
//   // Populate `out` from `path`. Return false on failure; do not log here.
//   bool OnLoad(std::string_view path, TEntry& out);
//
//   // Release any resources held by `entry` (GPU teardown, etc.).
//   // Called when the refcount reaches zero or on destruction.
//   void OnFree(TEntry& entry);
//
//   // Return true if this slot is occupied (entry has been successfully
//   // loaded and not yet freed). Used by Resolve() to reject stale handles
//   // that point at a freed-but-not-yet-reused slot.
//   bool IsEntryLive(const TEntry& entry) const;
//
// THandle requirements:
//   - uint32_t Id field
//   - bool IsValid() const
//
// TEntry requirements:
//   - uint32_t  Generation = 0
//   - uint32_t  RefCount   = 0
//   - std::string PathKey  (empty for non-path entries)
//=============================================================================

namespace
{
    constexpr uint32_t kAssetCacheIndexBits    = 20u;
    constexpr uint32_t kAssetCacheIndexMask    = (1u << kAssetCacheIndexBits) - 1u;
    constexpr uint32_t kAssetCacheMaxGeneration = (1u << (32u - kAssetCacheIndexBits)) - 1u;
} // namespace

template<typename TDerived, typename THandle, typename TEntry>
class AssetCache : public IService, public ILifetimeOwner
{
public:
    // -- Load from filesystem -------------------------------------------------
    //
    // Deduplicated: identical paths return the same handle. RefCount is
    // incremented on every call. Returns an invalid handle on load failure.
    [[nodiscard]] THandle Acquire(std::string_view path)
    {
        const std::string key(path);

        if (auto it = PathLookup.find(key); it != PathLookup.end())
        {
            THandle handle = it->second;
            if (TEntry* entry = Resolve(handle))
                ++entry->RefCount;
            return handle;
        }

        TEntry entry{};
        if (!Derived().OnLoad(path, entry))
            return {};

        THandle handle = AllocHandle(std::move(entry));
        if (handle.IsValid())
        {
            Resolve(handle)->PathKey = key;
            PathLookup.emplace(key, handle);
        }

        return handle;
    }

    // RAII variant. Same semantics as Acquire(); wraps the handle in a
    // LifetimeHandle that calls Release() automatically on destruction.
    [[nodiscard]] LifetimeHandle<TDerived, THandle> AcquireOwned(std::string_view path)
    {
        THandle handle = Acquire(path);
        if (!handle.IsValid())
            return {};

        // Acquire() already incremented RefCount -- skip Attach to avoid double-counting.
        return LifetimeHandle<TDerived, THandle>(this, handle,
            LifetimeHandle<TDerived, THandle>::NoAttach);
    }

    // Decrement the refcount. Frees resources and returns the slot to the pool
    // when the count reaches zero. Calling Release on an invalid handle is a no-op.
    void Release(THandle handle)
    {
        TEntry* entry = Resolve(handle);
        if (!entry) return;

        assert(entry->RefCount > 0 && "AssetCache: Release called on zero-refcount entry");
        if (entry->RefCount == 0) return;

        --entry->RefCount;
        if (entry->RefCount > 0) return;

        FreeEntry(DecodeIndex(handle.Id), *entry);
    }

protected:
    // Called by the derived destructor to free all live entries.
    void FreeAllEntries()
    {
        for (size_t i = 1; i < Entries.size(); ++i)
        {
            TEntry& entry = Entries[i];
            if (Derived().IsEntryLive(entry))
                Derived().OnFree(entry);
        }
    }

    // AllocHandle is protected so derived classes can use it for non-path
    // creation paths (e.g. TextureCache::CreateFromImage).
    [[nodiscard]] THandle AllocHandle(TEntry entry)
    {
        uint32_t index = 0;
        entry.RefCount = 1;

        if (!FreeSlots.empty())
        {
            index = FreeSlots.back();
            FreeSlots.pop_back();

            uint32_t gen = Entries[index].Generation + 1u;
            if (gen == 0u || gen > kAssetCacheMaxGeneration) gen = 1u;
            entry.Generation = gen;
            Entries[index] = std::move(entry);
        }
        else
        {
            index = static_cast<uint32_t>(Entries.size());
            entry.Generation = 1u;
            Entries.emplace_back(std::move(entry));
        }

        return MakeHandle(index, Entries[index].Generation);
    }

    [[nodiscard]] TEntry* Resolve(THandle handle)
    {
        if (!handle.IsValid()) return nullptr;
        const uint32_t index = DecodeIndex(handle.Id);
        const uint32_t gen   = DecodeGeneration(handle.Id);
        if (index == 0 || index >= Entries.size()) return nullptr;
        TEntry& entry = Entries[index];
        if (entry.Generation != gen || !Derived().IsEntryLive(entry)) return nullptr;
        return &entry;
    }

    [[nodiscard]] const TEntry* Resolve(THandle handle) const
    {
        if (!handle.IsValid()) return nullptr;
        const uint32_t index = DecodeIndex(handle.Id);
        const uint32_t gen   = DecodeGeneration(handle.Id);
        if (index == 0 || index >= Entries.size()) return nullptr;
        const TEntry& entry = Entries[index];
        if (entry.Generation != gen || !Derived().IsEntryLive(entry)) return nullptr;
        return &entry;
    }

    // Slot 0 is permanently reserved so that Handle::Id == 0 is always invalid.
    // Derived constructors must call this once during setup.
    void ReserveNullSlot()
    {
        assert(Entries.empty() && "ReserveNullSlot must be called before any entries are added");
        Entries.emplace_back();
    }

private:
    // ILifetimeOwner -- called by LifetimeHandle on construction / destruction.
    void Attach(uint64_t token) override
    {
        THandle handle{};
        std::memcpy(&handle, &token, sizeof(handle));
        if (TEntry* entry = Resolve(handle))
            ++entry->RefCount;
    }

    void Detach(uint64_t token) override
    {
        THandle handle{};
        std::memcpy(&handle, &token, sizeof(handle));
        Release(handle);
    }

    void FreeEntry(uint32_t index, TEntry& entry)
    {
        if (!entry.PathKey.empty())
        {
            PathLookup.erase(entry.PathKey);
            entry.PathKey.clear();
        }

        Derived().OnFree(entry);

        // Generation stays so the next AllocHandle can bump it.
        entry.RefCount = 0;
        FreeSlots.push_back(index);
    }

    [[nodiscard]] static THandle MakeHandle(uint32_t index, uint32_t generation)
    {
        THandle h{};
        h.Id = (generation << kAssetCacheIndexBits) | (index & kAssetCacheIndexMask);
        return h;
    }

    [[nodiscard]] static uint32_t DecodeIndex(uint32_t id)      { return id & kAssetCacheIndexMask; }
    [[nodiscard]] static uint32_t DecodeGeneration(uint32_t id) { return id >> kAssetCacheIndexBits; }

    TDerived& Derived() { return static_cast<TDerived&>(*this); }
    const TDerived& Derived() const { return static_cast<const TDerived&>(*this); }

    std::vector<TEntry>                          Entries;
    std::vector<uint32_t>                        FreeSlots;
    std::unordered_map<std::string, THandle>     PathLookup;
};
