#pragma once

#include "EditorDocument.h"

#include "viewport/WorldViewSettings.h"

#include <zone/ContentRiskRecord.h>
#include <zone/WorldPartitionIndex.h>
#include <zone/WorldPartitionManifest.h>

#include <cassert>
#include <filesystem>
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

struct MigratedTransitionLink
{
    TransitionId Forward;
    TransitionId Reverse;
    LinkId Link;
};

struct LegacyTransitionMigrationReport
{
    std::vector<MigratedTransitionLink> Links;
    std::vector<TransitionId> Unresolved;
    std::size_t CollapsedPairs = 0;
};

// The container above EditorDocument: the authored partition manifest plus the set
// of open zone documents. Exactly one of two modes at a time:
//   Legacy: no manifest; one anonymous zone document (a bare .sscene). Every
//           existing single-level behavior routes through this mode unchanged.
//   World:  manifest-backed (.sworld); zones open and close individually.
// Constructed in legacy mode; LoadWorld and NewWorld switch to world mode.
class WorldDocument
{
public:
    // Where every document this world owns resolves asset:// scene sources.
    // EditorServices supplies the project's roots; cook drivers supply their
    // assets root. Applied to the documents that already exist and to every
    // one created afterwards.
    void SetContentRoots(std::vector<std::filesystem::path> roots);

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
    bool SaveWorld();
    // After a save lands source files on disk: every open document that
    // depends on one of them re-projects from the fresh content.
    void PropagateSavedSources(std::span<const std::string> savedSources);                                // world file + every dirty zone document + sidecar
    // Enforces the .sworld extension (replacing whatever the dialog produced)
    // so saved worlds always round-trip through the extension fast path.
    bool SaveWorldAs(std::string_view path);
    void NewWorld(std::string_view name);            // one minted graph and zone, focus on it

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

    // Focus. SetFocusZone loads the zone if needed, then fires
    // OnEditedDocumentChanged (after the switch; the workspace uses it to reset
    // interaction state).
    // FocusWorldScene focuses the world scene instead of any zone: while it is
    // focused FocusZone() is invalid and every zone is context.
    bool SetFocusZone(ZoneId zone);
    bool FocusWorldScene();
    [[nodiscard]] bool IsWorldSceneFocused() const { return WorldSceneFocused_; }
    [[nodiscard]] ZoneId FocusZone() const { return FocusZone_; }
    // Zone selection is shared presentation state, distinct from the active
    // editing document. Single-clicking a graph/world row selects; explicit
    // focus (normally double-click) loads and activates it.
    bool SelectZone(ZoneId zone);
    [[nodiscard]] ZoneId SelectedZone() const { return SelectedZone_; }
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
    [[nodiscard]] GraphId MintGraphId();
    [[nodiscard]] DockId MintDockId();
    [[nodiscard]] LinkId MintLinkId();

    // Manifest edits go through verbs so the index rebuild and dirty flag cannot
    // be forgotten. Nothing writes Manifest() fields raw.
    ZoneId AddZone(GraphId graph, std::string name);        // mints id, empty SceneRef until first save
    GraphId AddGraph(std::string name);
    bool RenameZone(ZoneId zone, std::string name);
    bool SetZoneGraph(ZoneId zone, GraphId graph);
    bool SetZoneBounds(ZoneId zone, Aabb3d bounds, bool overridden = true);
    bool UseDerivedZoneBounds(ZoneId zone);
    bool RenameGraph(GraphId graph, std::string name);

    // Graph streaming overrides; nullopt clears the field back to inherited.
    bool SetGraphHopCount(GraphId graph, std::optional<int32_t> hopCount);
    bool SetGraphRadius(GraphId graph, std::optional<double> radius);
    bool SetGraphResidentCap(GraphId graph, std::optional<int32_t> cap);

    // Explicit migration window for v1 manifests. Teleports have enough data
    // to become non-spatial world links. Geometric records have no plane
    // transform in the legacy format and remain unresolved instead of being
    // guessed. New-format saves refuse unresolved records.
    LegacyTransitionMigrationReport MigrateLegacyTransitions();
    // Explicitly reclassifies one legacy record (and its reciprocal record, if
    // present) as a non-spatial WorldLink. This is intentionally separate from
    // automatic migration because Doorway/Seam records normally require Docks.
    bool ConvertLegacyTransitionToTeleport(TransitionId transition);
    bool DiscardLegacyTransition(TransitionId transition);

    // Installs a whole manifest, reconciling everything derived from it: the
    // spatial index, the dirty flag, the selection and any open zone whose
    // header vanished, and the focus when the focused zone is gone. This is the
    // single transition an undoable manifest edit runs through, so a rolled-back
    // structural change cannot leave the session pointing at a zone that no
    // longer exists.
    void ApplyManifestSnapshot(WorldPartitionManifest manifest);

    // Refreshes derived zone bounds, then reruns validation. Manifest verbs
    // revalidate themselves; content edits (moving entities) call this
    // explicitly, and save runs it implicitly.
    void Revalidate();

    // Fires after the edited document changes: a zone focus change, a focus
    // switch to the world scene, or a wholesale replacement (new, open, close to
    // legacy). Every path that leaves a different document under the editing
    // stack raises it, so the workspace rebinds without the caller arranging it.
    std::function<void()> OnEditedDocumentChanged;

    // Fires immediately before the open documents are replaced wholesale (world
    // load, new world, close-to-legacy, legacy new and load), while every one of
    // them is still alive. Receivers must terminate anything that captured
    // document state: a preview entity or a captured mesh reverts here or not at
    // all. Validation runs first, so a rejected file never fires this.
    std::function<void()> OnWillReplaceDocuments;

    // Fires after a zone document is destroyed by UnloadZone. The workspace
    // clears the undo stack on it: queued commands may hold references into any
    // open zone document (the cross-zone move holds two), and an unload would
    // dangle them. Narrower than the focus-change reset: focus did not change,
    // so the tool context, sink, and selection stay.
    std::function<void(ZoneId)> OnZoneUnloaded;

private:
    // Each open zone document gets a unique RegistryId {NextRegistryIndex_++, 1},
    // starting at 2, never reused within a session, so a stale SelectableRef
    // can never alias a later-opened zone.
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
    // Recomputes each non-overridden coarse AABB from open Zone content.
    void RefreshDerivedZoneBounds();
    // A fresh world scene document with its own registry identity, loaded from
    // the manifest's WorldSceneRef when that resolves to a file.
    void CreateWorldSceneDocument();
    [[nodiscard]] std::string UserSidecarPath() const;
    void WriteUserSidecar() const;
    // Opens the zones the sidecar recorded and applies their visibility;
    // returns the recorded focus (invalid when absent or malformed).
    SidecarFocus ApplyUserSidecar();
    // Persists the outgoing world's sidecar and tears the world state down,
    // leaving no editable document behind. Callers install one.
    void CloseWorldState();
    // Persists the outgoing world's sidecar and returns to legacy mode with a
    // fresh single document.
    void CloseWorldToLegacy();
    // Runs OnWillReplaceDocuments if anything is listening. Every path that
    // destroys or replaces an open document calls this while it is still alive.
    void NotifyWillReplaceDocuments();
    // Runs OnEditedDocumentChanged if anything is listening.
    void NotifyEditedDocumentChanged();

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
    ZoneId SelectedZone_;
    bool WorldSceneFocused_ = false;
    WorldViewSettings* ViewSettings_ = nullptr;
    std::unordered_map<ZoneId, OpenZone> OpenZones_;
    std::unique_ptr<EditorDocument> WorldScene_;
    std::unique_ptr<EditorDocument> LegacyDocument_;
    ZoneViewState LegacyView_;

    std::vector<std::filesystem::path> ContentRoots_;
    uint16_t NextRegistryIndex_ = 2;
    std::mt19937_64 Rng_;
};
