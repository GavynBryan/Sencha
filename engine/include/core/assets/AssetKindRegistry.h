#pragma once

#include <core/assets/AssetStager.h>
#include <core/assets/AssetStore.h>

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// AssetKindRegistration
//
// Everything the orchestration layers need to know about one outer asset kind.
// Before this existed the same knowledge was spelled out as a switch over
// AssetType in the front door, the preloader (three times), the hot reloader,
// and the project scanner; adding a kind meant editing all of them. A kind now
// registers once and every driver reads the record.
//
// Commit and Reload are registered operations, not virtuals on IAssetStager:
// the registration site already names the concrete loader, so its CommitTyped
// keeps returning the loader's own handle type and only the lease crosses the
// type-erased boundary. Both run on the owner thread.
//
// Store and Commit travel together: both name the cache, so a host that wired
// no cache for a kind registers neither. Stager is independent of both because
// staging is a pure decode that touches no cache, which is what lets a headless
// host stage an asset it cannot commit. Reload additionally requires Commit.
// A kind with none of the three is scan-only: its extensions still register so
// the scanner can classify files it must not try to load.
//=============================================================================
struct AssetKindRegistration
{
    AssetType Type = AssetType::Unknown;
    std::string Name;

    // Extensions of the runtime-loadable form, leading dot included. Used to
    // classify files during a content scan.
    std::vector<std::string> RuntimeExtensions;

    IAssetStager* Stager = nullptr;
    IAssetStore* Store = nullptr;

    // The kind's list form (a mesh's per-slot materials), when it has one.
    // Requires Store: a list is made of that store's tokens.
    IAssetListStore* ListStore = nullptr;

    // Commits a staged payload and yields the creation reference.
    std::function<AssetLease(AssetStaging&&)> Commit;

    // Swaps a resident entry in place, keeping its handle. Empty when the kind
    // cannot reload.
    std::function<bool(AssetStaging&&)> Reload;

    [[nodiscard]] bool IsLoadable() const
    {
        return Stager != nullptr && Store != nullptr && Commit != nullptr;
    }
};

//=============================================================================
// AssetKindRegistry
//
// Registration-ordered table of the outer asset kinds. Small and linear on
// purpose: a handful of entries, walked rarely, and iteration order is
// registration order so scans and diagnostics stay deterministic without a
// second identity map.
//=============================================================================
class AssetKindRegistry
{
public:
    // Rejects a duplicate type, an unnamed kind, a partially wired loadable
    // kind, a list store without its single store, and an extension another
    // kind already claims.
    [[nodiscard]] bool Register(AssetKindRegistration registration);

    [[nodiscard]] const AssetKindRegistration* Find(AssetType type) const;
    [[nodiscard]] AssetKindRegistration* Find(AssetType type);

    [[nodiscard]] AssetType FindTypeByExtension(std::string_view extension) const;
    [[nodiscard]] std::span<const AssetKindRegistration> Entries() const;

private:
    std::vector<AssetKindRegistration> Registrations;
};

// Identity (name and runtime extensions) of one engine built-in kind, with no
// loader or store attached. This is the single extension table: the front door
// fills in the load half on top of it, and a tool that only needs to classify
// files registers the identity alone. An unknown type yields an unregisterable
// record.
[[nodiscard]] AssetKindRegistration MakeBuiltinAssetKind(AssetType type);

// The built-in kinds, in registration order.
[[nodiscard]] std::span<const AssetType> BuiltinAssetKinds();

// Every built-in kind registered identity-only. For scans that run without an
// AssetSystem (cook tools, tests). A host that can reach its AssetSystem uses
// Kinds() instead, so kinds a game module registered are visible too.
[[nodiscard]] const AssetKindRegistry& BuiltinAssetKindRegistry();
