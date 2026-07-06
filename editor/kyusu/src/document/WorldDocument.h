#pragma once

#include "EditorDocument.h"

#include "viewport/WorldViewSettings.h"

#include <zone/ContentRiskRecord.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>

#include <cassert>
#include <functional>
#include <memory>
#include <random>
#include <span>
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
    ~WorldDocument();   // writes the user sidecar so view state survives exit

    // Forwarded to every zone document, present and future.
    void SetAssetEnvironment(RuntimeAssets& assets);

    // The provider zone documents log through; command factories that refuse an
    // operation report through it too.
    [[nodiscard]] LoggingProvider& Logging() { return Logging_; }

    // Non-owning; the workspace's world view state rides the user sidecar with
    // the zone view states, so it persists per user alongside them.
    void BindViewSettings(WorldViewSettings* settings) { ViewSettings_ = settings; }
    [[nodiscard]] WorldViewSettings* ViewSettings() { return ViewSettings_; }

    // Mode and files.
    [[nodiscard]] bool IsWorld() const { return WorldMode_; }
    [[nodiscard]] std::string_view WorldPath() const { return WorldPath_; }

    // Absolute path for a zone's project-relative scene ref (resolved against
    // the world file's directory). The world cook reads authored scenes through
    // this so it opens exactly the files the editor saves.
    [[nodiscard]] std::string ResolveScenePath(std::string_view sceneRef) const;
    // True when the file parses as a world manifest (format_version plus a
    // zones array). Open routing sniffs content, not just the extension: a
    // world saved under the wrong extension must still open as a world.
    [[nodiscard]] static bool IsWorldManifestFile(std::string_view path);
    bool LoadWorld(std::string_view path);          // parses .sworld; sidecar or start zone picks focus
    bool SaveWorld();                                // world file + every dirty zone document + sidecar
    // Enforces the .sworld extension (replacing whatever the dialog produced)
    // so saved worlds always round-trip through the extension fast path.
    bool SaveWorldAs(std::string_view path);
    void NewWorld(std::string_view name);            // one minted region and zone, focus on it

    // True when Save can write without asking for a path: the world file in
    // world mode, the focus document's file otherwise.
    [[nodiscard]] bool HasSaveTarget() const;

    // Legacy single-document file actions. Called in world mode they close the
    // world (persisting its sidecar) and drop back to legacy mode first.
    void New();
    bool Load(std::string_view path);
    bool Save();
    bool SaveAs(std::string_view path);

    // Manifest validation plus the editor-side scene-resolvability check, rerun
    // on world load, save, and every manifest verb. Errors are logged as they
    // are produced; the records stay here for the partition panel to display.
    [[nodiscard]] std::span<const ContentRiskRecord> ValidationRecords() const
    {
        return ValidationRecords_;
    }

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

    // An open zone's document, for cross-zone operations (the move command).
    // Null when the zone is not open or the document is in legacy mode.
    [[nodiscard]] EditorDocument* ZoneDocument(ZoneId zone);

    // The world scene: authored global content, one document per world, open
    // whenever the world is open. It is not a zone: no ZoneId, no view state,
    // no bounds, never in OpenZones_ or the manifest zone list. World mode
    // only (asserts).
    [[nodiscard]] EditorDocument& WorldSceneDocument();
    [[nodiscard]] const EditorDocument& WorldSceneDocument() const;

    // Focus. SetFocusZone loads the zone if needed, then fires OnFocusChanged
    // (after the switch; the workspace uses it to reset interaction state).
    // FocusWorldScene focuses the world scene instead of any zone: while it is
    // focused FocusZone() is invalid and every zone is context.
    bool SetFocusZone(ZoneId zone);
    bool FocusWorldScene();
    [[nodiscard]] bool IsWorldSceneFocused() const { return WorldSceneFocused_; }
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

    // Region streaming overrides; nullopt clears the field back to inherited.
    bool SetRegionHopCount(RegionId region, std::optional<int32_t> hopCount);
    bool SetRegionRadius(RegionId region, std::optional<double> radius);
    bool SetRegionResidentCap(RegionId region, std::optional<int32_t> cap);

    // Transition verbs, same non-undoable discipline. AddTransition mints the
    // edge and nothing else; reverse pairing is the caller's explicit second
    // call. Returns the new id.
    TransitionId AddTransition(ZoneId from, ZoneId to, TransitionTopology topology,
                               bool oneWay, int32_t preloadPriority);
    bool RemoveTransition(TransitionId transition);
    bool RenameTransition(TransitionId transition, std::string name);
    bool SetTransitionTopology(TransitionId transition, TransitionTopology topology);
    bool SetTransitionOneWay(TransitionId transition, bool oneWay);
    bool SetTransitionPreloadPriority(TransitionId transition, int32_t priority);
    bool SetTransitionRequiredTags(TransitionId transition, std::vector<std::string> tags);
    bool SetTransitionPreloadDepth(TransitionId transition, int32_t depth);

    // Refreshes derived zone bounds, then reruns validation. Manifest verbs
    // revalidate themselves; content edits (moving entities) call this
    // explicitly, and save runs it implicitly.
    void Revalidate();

    std::function<void()> OnFocusChanged;

    // Fires after a zone document is destroyed by UnloadZone. The workspace
    // clears the undo stack on it: queued commands may hold references into any
    // open zone document (the cross-zone move holds two), and an unload would
    // dangle them. Narrower than the focus-change reset: focus did not change,
    // so the tool context, sink, and selection stay.
    std::function<void(ZoneId)> OnZoneUnloaded;

private:
    // Each open zone document gets a unique RegistryId {NextRegistryIndex_++, 1},
    // starting at 2, never reused within a session (the ZoneRuntime discipline),
    // so a stale SelectableRef can never alias a later-opened zone.
    struct OpenZone
    {
        std::unique_ptr<EditorDocument> Document;
        ZoneViewState View;
    };

    // What the user sidecar recorded as focused: a zone, or the world scene.
    struct SidecarFocus
    {
        ZoneId Zone;
        bool WorldScene = false;
    };

    [[nodiscard]] const ZoneHeader* FindZoneHeader(ZoneId zone) const;
    [[nodiscard]] uint64_t MintRawId();
    void MarkManifestEdited();
    void AssignSceneRefsForNewZones();
    void RunValidation();
    // Recomputes non-overridden zone bounds from live content in open zones, so
    // validation and the manifest write see current spans.
    void RefreshDerivedZoneBounds();
    // A fresh world scene document with its own registry identity, loaded from
    // the manifest's WorldSceneRef when that resolves to a file.
    void CreateWorldSceneDocument();
    [[nodiscard]] std::string UserSidecarPath() const;
    void WriteUserSidecar() const;
    // Opens the zones the sidecar recorded and applies their visibility;
    // returns the recorded focus (invalid when absent or malformed).
    SidecarFocus ApplyUserSidecar();
    // Persists the outgoing world's sidecar and returns to legacy mode with a
    // fresh single document.
    void CloseWorldToLegacy();

    LoggingProvider& Logging_;
    RuntimeAssets* Assets_ = nullptr;

    bool WorldMode_ = false;
    std::string WorldPath_;
    WorldPartitionManifest Manifest_;
    mutable WorldPartitionIndex Index_;
    mutable bool IndexDirty_ = true;
    bool WorldDirty_ = false;
    std::vector<ContentRiskRecord> ValidationRecords_;

    ZoneId FocusZone_;
    bool WorldSceneFocused_ = false;
    WorldViewSettings* ViewSettings_ = nullptr;
    std::unordered_map<ZoneId, OpenZone> OpenZones_;
    std::unique_ptr<EditorDocument> WorldScene_;
    std::unique_ptr<EditorDocument> LegacyDocument_;
    ZoneViewState LegacyView_;

    uint16_t NextRegistryIndex_ = 2;
    std::mt19937_64 Rng_;
};
