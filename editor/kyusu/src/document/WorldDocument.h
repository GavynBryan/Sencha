#pragma once

#include "EditorDocument.h"

#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>

#include <cassert>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>

// Per-zone editor view state. Editability is NOT here: the focus zone is the one
// editable zone, derived, never stored.
struct ZoneViewState
{
    bool VisibleInEditor = true;
};

// The container above EditorDocument: the authored partition manifest plus the set
// of open zone documents. Exactly one of two modes at a time:
//   Legacy: no manifest; one anonymous zone document (a bare .level.json). Every
//           existing single-level behavior routes through this mode unchanged.
//   World:  manifest-backed (.sworld); zones open and close individually.
// Constructed in legacy mode; LoadWorld and NewWorld switch to world mode.
class WorldDocument
{
public:
    explicit WorldDocument(LoggingProvider& logging);

    // Forwarded to every zone document, present and future.
    void SetAssetEnvironment(RuntimeAssets& assets);

    // Mode and files.
    [[nodiscard]] bool IsWorld() const { return WorldMode_; }
    bool LoadWorld(std::string_view path);          // parses .sworld, loads start zone as focus
    bool SaveWorld();                                // world file + every dirty zone document
    bool SaveWorldAs(std::string_view path);
    void NewWorld(std::string_view name);            // one minted region and zone, focus on it

    // Legacy passthroughs to the single zone document (world mode routes file
    // actions through LoadWorld/SaveWorld instead).
    void New();
    bool Load(std::string_view path);
    bool Save();
    bool SaveAs(std::string_view path);

    [[nodiscard]] WorldPartitionManifest& Manifest();          // World mode only (asserts)
    [[nodiscard]] const WorldPartitionIndex& Index() const;    // rebuilt on manifest edits

    // Zone lifecycle. LoadZone deserializes the zone's scene into a fresh
    // EditorDocument (Context state). UnloadZone refuses (returns false, logs)
    // when that zone document is dirty or is the focus zone; saving or focusing
    // elsewhere first is the caller's job.
    bool LoadZone(ZoneId zone);
    bool UnloadZone(ZoneId zone);
    [[nodiscard]] bool IsZoneOpen(ZoneId zone) const;
    bool SetZoneVisible(ZoneId zone, bool visible);

    // Focus. SetFocusZone loads the zone if needed, then fires OnFocusChanged
    // (after the switch; the workspace uses it to reset interaction state).
    bool SetFocusZone(ZoneId zone);
    [[nodiscard]] ZoneId FocusZone() const { return FocusZone_; }
    [[nodiscard]] EditorDocument& FocusDocument();
    [[nodiscard]] const EditorDocument& FocusDocument() const;

    // Deterministic iteration for rendering and the tree: manifest order.
    // fn(ZoneId, EditorDocument&, const ZoneViewState&), open zones only. Legacy
    // mode visits the single anonymous document.
    template <typename Fn>
    void VisitOpenZones(Fn&& fn)
    {
        if (!WorldMode_)
        {
            if (LegacyDocument_)
                fn(ZoneId{}, *LegacyDocument_, LegacyView_);
            return;
        }
        for (const ZoneHeader& zone : Manifest_.Zones)
        {
            const auto it = OpenZones_.find(zone.Id);
            if (it == OpenZones_.end())
                continue;
            fn(zone.Id, *it->second.Document, it->second.View);
        }
    }

    [[nodiscard]] bool IsDirty() const;   // any zone document dirty, or the manifest edited

    // Id minting: random nonzero 64-bit, re-rolled on collision with any id
    // already in the manifest. Editor-side only by design; the engine mints
    // no random ids.
    [[nodiscard]] ZoneId MintZoneId();
    [[nodiscard]] RegionId MintRegionId();
    [[nodiscard]] TransitionId MintTransitionId();

    // Manifest edits go through verbs so the index rebuild and dirty flag cannot
    // be forgotten. Nothing writes Manifest() fields raw.
    ZoneId AddZone(RegionId region, std::string name);        // mints id, empty SceneRef until first save
    RegionId AddRegion(std::string name);
    bool RenameZone(ZoneId zone, std::string name);
    bool RenameRegion(RegionId region, std::string name);

    std::function<void()> OnFocusChanged;

private:
    // Each open zone document gets a unique RegistryId {NextRegistryIndex_++, 1},
    // starting at 2, never reused within a session (the ZoneRuntime discipline),
    // so a stale SelectableRef can never alias a later-opened zone.
    struct OpenZone
    {
        std::unique_ptr<EditorDocument> Document;
        ZoneViewState View;
    };

    [[nodiscard]] const ZoneHeader* FindZoneHeader(ZoneId zone) const;
    [[nodiscard]] std::string ResolveScenePath(std::string_view sceneRef) const;
    [[nodiscard]] uint64_t MintRawId();
    void MarkManifestEdited();
    void AssignSceneRefsForNewZones();

    LoggingProvider& Logging_;
    RuntimeAssets* Assets_ = nullptr;

    bool WorldMode_ = false;
    std::string WorldPath_;
    WorldPartitionManifest Manifest_;
    mutable WorldPartitionIndex Index_;
    mutable bool IndexDirty_ = true;
    bool WorldDirty_ = false;

    ZoneId FocusZone_;
    std::unordered_map<ZoneId, OpenZone> OpenZones_;
    std::unique_ptr<EditorDocument> LegacyDocument_;
    ZoneViewState LegacyView_;

    uint16_t NextRegistryIndex_ = 2;
    std::mt19937_64 Rng_;
};
