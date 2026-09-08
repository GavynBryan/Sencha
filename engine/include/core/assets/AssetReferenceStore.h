#pragma once

#include <core/assets/AssetCache.h>
#include <core/assets/AssetStager.h>

#include <any>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

//=============================================================================
// AssetReferenceStore<THandle, TAssetType>
//
// A store for one asset kind that holds identity and nothing else: a path
// resolves to a generational handle, the handle resolves back to the path,
// references are counted, and no byte of the asset is ever read. It is the
// kind's stager and its store at once, so it registers through the same
// RegisterAssetKind as a real loader-and-cache pair.
//
// This exists for processes that must carry a reference through without being
// able to hold what it names. A headless cook writes a scene's mesh field into
// the cooked scene, and a mesh is GPU-resident, so a cook composed without
// graphics cannot load one; before this it could not name one either, because
// a field whose load left the handle invalid has no path to save. With a
// reference store registered as the kind, the load interns the path, the save
// reads it back, and the cooked output is what the windowed cook would have
// written.
//
// A reference store is never a substitute for the real cache in a process
// that draws: resolving one of its handles to geometry is a type error, not a
// missing asset, because nothing accepts this store where a cache is expected.
//=============================================================================
struct AssetReferenceEntry
{
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

template<typename THandle, AssetType TAssetType>
class AssetReferenceStore final
    : public AssetCache<AssetReferenceStore<THandle, TAssetType>,
                        THandle,
                        AssetReferenceEntry,
                        TAssetType>
    , public IAssetStager
{
    using Base = AssetCache<AssetReferenceStore, THandle, AssetReferenceEntry, TAssetType>;
    friend Base;

public:
    AssetReferenceStore()
    {
        Base::ReserveNullSlot();
    }

    ~AssetReferenceStore() override
    {
        Base::FreeAllEntries();
    }

    AssetReferenceStore(const AssetReferenceStore&) = delete;
    AssetReferenceStore& operator=(const AssetReferenceStore&) = delete;

    // The stage half reads nothing: the registry record that resolved this
    // path is the whole of what the store keeps. The payload is the record's
    // path so the staging is valid by the front door's rule (no error, a
    // payload present) and the commit has something to intern.
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record, IAssetSource&) override
    {
        AssetStaging staging;
        staging.Record = record;
        staging.Payload = record.Path;
        return staging;
    }

    // The commit half: intern the path. A path already interned returns its
    // existing handle with one more reference, matching every cache's
    // deduplication rule.
    [[nodiscard]] THandle CommitTyped(AssetStaging&& staged)
    {
        const std::string* path = std::any_cast<std::string>(&staged.Payload);
        if (path == nullptr || path->empty())
            return {};
        return Intern(*path);
    }

    [[nodiscard]] THandle Intern(std::string_view path)
    {
        AssetReferenceEntry entry;
        entry.Alive = true;
        return Base::AllocNamedHandle(path, std::move(entry));
    }

private:
    void OnFree(AssetReferenceEntry& entry)
    {
        entry.Alive = false;
    }

    [[nodiscard]] bool IsEntryLive(const AssetReferenceEntry& entry) const
    {
        return entry.Alive;
    }
};
