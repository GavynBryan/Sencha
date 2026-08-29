#pragma once

#include "EntitySnapshot.h"
#include "EditorScene.h"

#include "scene_source/SceneComposition.h"
#include "scene_source/SceneSourceCache.h"
#include "scene_source/SceneSourceDocument.h"

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>
#include <world/registry/Registry.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <functional>
#include <unordered_map>
#include <unordered_set>

class LoggingProvider;
class AssetSystem;
class AssetRegistry;
struct RuntimeAssets;
struct IComponentSerializer;

class PersistentEntityIndex;
class EditorDocument
{
public:
    // The logger is always present (a sink-less LoggingProvider is a silent
    // no-op, which is how headless cooks and tests run), so the document always
    // serializes through a SceneSerializationContext: one path, no "is logging
    // wired yet" branch. The asset system is the separately-optional half.
    explicit EditorDocument(LoggingProvider& logging);

    // Binds the engine asset system the document serializes through (one pipeline,
    // shared with the cook and runtime): StaticMeshComponent and other asset-handle
    // fields round-trip handle<->asset:// path through this. Also wires the
    // document World's StaticMeshComponentAssets resource so mesh/material handles
    // stay retained while authored. Until set, the context carries a null asset
    // system, which is the brush-only path (no asset fields to resolve).
    void SetAssetEnvironment(RuntimeAssets& assets);

    // The shared asset system and its registry, for tooling that resolves asset
    // refs (the inspector's asset-field picker). Null until SetAssetEnvironment.
    [[nodiscard]] AssetSystem* GetAssetSystem() const { return Assets; }
    [[nodiscard]] const AssetRegistry* GetAssetCatalog() const { return Catalog; }

    // Hands the document its registry identity (WorldDocument assigns each open
    // zone document a unique, never-reused RegistryId so a stale SelectableRef
    // cannot alias a later-opened zone). Callable only while the document is
    // empty: entities already created would carry the old identity.
    void SetRegistryIdentity(RegistryId id, ZoneId zone);

    [[nodiscard]] std::string_view GetDisplayName() const;
    [[nodiscard]] bool IsDirty() const;
    bool Save();
    bool SaveAs(std::string_view path);
    bool Load(std::string_view path);
    void New();

    // Where asset://... source references resolve on disk. Set before Load on
    // any document that may hold scene instances; a document with none never
    // consults them.
    void SetContentRoots(std::vector<std::filesystem::path> roots);

    // What resolution and expansion of this document's instances reported.
    // Empty when everything resolved; the cook refuses on anything here, the
    // editor shows it and keeps working.
    struct ProjectionDiagnostics
    {
        std::string ResolveError; // cycle or unresolvable source: nothing expanded
        std::vector<std::string> MissingIds;
        std::vector<std::string> DanglingOverrides;
        [[nodiscard]] bool Clean() const
        {
            return ResolveError.empty() && MissingIds.empty()
                && DanglingOverrides.empty();
        }
    };
    [[nodiscard]] const ProjectionDiagnostics& GetProjectionDiagnostics() const
    {
        return ProjectionDiagnostics_;
    }

    // Destroys the derived instance entities and expands the instance records
    // again. Load runs it; an explicit call re-projects after a record or
    // source change.
    void RebuildSceneProjection();

    // Fired after every rebuild, once the new projection is live. Projected
    // entities are destroyed and recreated, so anything holding their handles
    // -- the workspace's selection, above all -- has to re-resolve, and this
    // is how it learns without the document knowing who is listening.
    void SetProjectionObserver(std::function<void()> observer)
    {
        ProjectionObserver_ = std::move(observer);
    }

    // ── Scene instances ─────────────────────────────────────────────────────

    // Places `source` (asset://...sscene) at `placement`, minting the instance
    // id and an id for every entity the source contributes, and projects it.
    // Returns the invalid id with `error` set when the source does not
    // resolve. An authoring act: the document is dirty afterwards.
    SceneInstanceId PlaceSceneInstance(std::string source,
                                       const Transform3f& placement,
                                       PersistentEntityId parent = {},
                                       std::string* error = nullptr);
    // Re-adds a previously captured record verbatim -- the redo half of a
    // placement command, which is what keeps minted ids stable across
    // undo/redo. Refuses an id that is already placed.
    bool RestoreSceneInstance(SceneInstanceRecord record);
    // Removes the placement and its projection. The removed record, harvested
    // up to the moment of removal, lands in `removed` for the undo to restore.
    bool RemoveSceneInstance(SceneInstanceId id, SceneInstanceRecord* removed = nullptr);
    // Severs the link: every entity the placement contributed becomes a plain
    // local entity of this document, nested content flattened, and the record
    // is gone. The captured record is the undo's rebuild input.
    bool BreakSceneInstance(SceneInstanceId id, SceneInstanceRecord* broken = nullptr);

    [[nodiscard]] const SceneInstanceRecord* FindSceneInstance(SceneInstanceId id) const;
    // A derived, non-root member of a placement: editable in place, not
    // restructurable -- reparenting it out has no override to land in.
    [[nodiscard]] bool IsSceneInstanceMember(EntityId entity) const;
    [[nodiscard]] bool IsSceneInstanceRoot(EntityId entity) const;
    // The source path of the placement the entity belongs to, empty for
    // entities outside any projection.
    [[nodiscard]] std::string SceneInstanceSourceOf(EntityId entity) const;

    // The owning placement's id for a projected entity, root and added
    // children included; invalid outside any instance. This is the identity
    // the cook stamps on expanded members: the outermost placement, because
    // an inner instance id repeats across two placements of the same source
    // and could not address one group.
    [[nodiscard]] SceneInstanceId SceneInstanceOwnerOf(EntityId entity) const;

    // Whether this document's placements reach `assetPath` -- directly, as a
    // nested source its resolve had to read, or as a record whose resolve
    // failed (a broken source that later saves valid must still trigger the
    // dependent). The saver asks this before ordering a re-projection.
    [[nodiscard]] bool DependsOnSource(std::string_view assetPath) const;

    // Forgets the given sources' cached parses and re-projects once, when
    // any of them is one this document depends on; false means none were and
    // nothing happened. The explicit half of source invalidation (§3):
    // timestamps alone can miss a same-second rewrite.
    bool ReloadDependentSources(std::span<const std::string> assetPaths);

    // This document's own identity as a source reference: the asset:// path
    // its file is known by under the content roots, or empty for an unsaved
    // document or one outside them.
    [[nodiscard]] std::string SourceAssetPath() const;

    // Records fresh ids for every instance path resolution reported missing
    // (a source that grew since the placement was recorded), then re-projects.
    // An authoring act: marks the document dirty. Never called by loads or
    // cooks -- a cook that minted would bake ids the source never recorded.
    void MintMissingInstanceIds();

    // One entity's components as the serializers say they are right now, as
    // an ordered Json5 object -- identity excluded, since it lives at record
    // level. The one shape the source build, the projection baselines, and
    // the harvest diffs all speak.
    [[nodiscard]] Json5Value SerializeEntityComponents(EntityId entity) const;

    // In-memory serialization to and from .sscene text. Save and Load are the
    // file-backed wrappers. Known component values always come from live
    // document state; everything this build does not know -- unknown
    // components, unknown fields, comments, instance records -- is carried
    // through the retained source, so a round trip loses nothing it did not
    // deliberately change.
    [[nodiscard]] std::string ToSceneText();
    bool LoadFromSceneText(std::string_view text, std::string* error = nullptr);

    // Captures an entity's full persistent state (every registered component via
    // the serializer registry, plus the brush sidecar mesh and view flags) so a
    // deletion can be undone. RestoreEntity recreates it and returns the new id
    // (a fresh generational handle: the original index/generation is not reused).
    // With freshMesh, a captured brush mesh is given a NEW BrushId instead of
    // re-seating at the source's id: required when the source is still alive (a
    // duplicate), so the two entities don't share one mesh. Undo-of-delete keeps
    // the default (freshMesh == false) to preserve stable ids.
    [[nodiscard]] EntitySnapshot CaptureEntity(EntityId entity) const;
    EntityId RestoreEntity(const EntitySnapshot& snapshot, bool freshMesh = false);

    // Deep copy of a live entity (capture + restore with a fresh brush mesh). The
    // one "clone a live entity" primitive; the duplicate command and the drag
    // preview both go through here.
    EntityId DuplicateEntity(EntityId source);

    // Captures one component's persistent state as JSON (asset fields as stable
    // paths), and restores it onto the same entity. Used to make component removal
    // undoable: capture, then the serializer's typed Remove releases asset refs;
    // restore re-resolves the paths on undo. Same round-trip as scene save/load.
    [[nodiscard]] JsonValue CaptureComponent(EntityId entity,
                                             const IComponentSerializer& serializer) const;
    bool RestoreComponent(EntityId entity, IComponentSerializer& serializer,
                          const JsonValue& snapshot);

    [[nodiscard]] bool HasFilePath() const;

    void MarkDirty(bool dirty = true);
    // Owner hook for derived editor state (world validation). It is invoked only
    // for authored mutations, never for load/save clearing dirty state.
    std::function<void()> OnEdited;
    [[nodiscard]] EditorScene& GetScene();
    [[nodiscard]] const EditorScene& GetScene() const;
    [[nodiscard]] const Registry& GetRegistry() const;

    // Document-wide fallback material applied to any face that carries no explicit
    // one (a fresh brush is never "no material"). A document setting. (04-§2)
    [[nodiscard]] const AssetRef& GetDefaultMaterial() const;
    void SetDefaultMaterial(AssetRef material);

private:
    // The lazily built source lookup over ContentRoots_.
    [[nodiscard]] SceneSourceCache& EnsureSourceCache();

    // Builds the document's current source form: live values merged over the
    // retained source's trivia and unknowns. ToSceneText renders it; Save also
    // scans it for unresolved asset references before writing.
    [[nodiscard]] SceneSourceDocument BuildSceneSource() const;

    // Folds what happened to the projected entities back into the instance
    // records: the root's transform into the placement, member edits into
    // sparse patches, added and removed components, entities added beneath the
    // projection, and deletions into suppressions. Runs before every save and
    // every re-projection, so the records are always the authority at rest.
    void HarvestInstanceOverrides();

    // The harvest's working state for one instance record, filled by the
    // three phases below in order.
    struct RecordHarvest
    {
        std::vector<std::pair<SceneElementPath, Json5Value>> Patches;
        std::vector<std::pair<SceneElementPath, Json5Value>> Added;
        std::vector<std::pair<SceneElementPath, std::vector<std::string>>> Removed;
        std::vector<SceneAddedEntity> AddedEntities;
        std::vector<SceneElementPath> Suppressed;
        // Seen distinguishes "the projection produced this placement's root"
        // from "this record was never projected at all" -- a freshly placed
        // record, or one whose source failed to resolve. The harvest can only
        // speak about what the projection produced.
        bool RootSeen = false;
        bool RootAlive = false;
    };
    void HarvestProjectedElements(
        const PersistentEntityIndex& index,
        std::unordered_map<std::uint64_t, RecordHarvest>& byInstance);
    void AbsorbAuthoredChildren(
        std::unordered_map<std::uint64_t, RecordHarvest>& byInstance);
    void FoldHarvestsIntoRecords(
        const PersistentEntityIndex& index,
        std::unordered_map<std::uint64_t, RecordHarvest>& byInstance);

    // One expanded entity the projection owns, keyed by its persistent id.
    struct ProjectedElement
    {
        SceneElementPath Path;
        SceneInstanceId Instance;
        bool Root = false;
        // The placement's own added entity (D4): harvested back into the
        // add_entities record, never into a patch.
        bool Added = false;
        // The entity's components as this document loaded them -- serializer
        // shape, post-override -- so a harvest diff sees only live edits.
        Json5Value Baseline;
    };

    std::string FilePath;
    bool Dirty = false;
    Registry Registry_;
    EditorScene Scene;
    // What the file said that the live registry does not model: unknown
    // components and fields, comments, instance records, root members from a
    // newer build. Loaded with the document, consulted on every save.
    SceneSourceDocument Retained_;

    std::vector<std::filesystem::path> ContentRoots_;
    std::unique_ptr<SceneSourceCache> SourceCache_;
    std::unordered_map<std::uint64_t, ProjectedElement> Projection_;
    // Entities absorbed into add_entities records by the last harvest, so the
    // source build leaves them out of the local entity list.
    std::unordered_set<std::uint64_t> AbsorbedPids_;
    ProjectionDiagnostics ProjectionDiagnostics_;
    // Announced after each rebuild; empty in hosts that hold no handles.
    std::function<void()> ProjectionObserver_;

    // The rebuild itself, with the many early exits its diagnostics need.
    // RebuildSceneProjection wraps it so the announcement has one home.
    void ExpandSceneProjection();
    [[nodiscard]] Json5Value SerializeSourceBaseline(
        const Json5Value& components, SceneSerializationContext& context);
    AssetRef DefaultMaterial{ AssetType::Material, "asset://materials/dev/gray.smat" };

    // Always present (constructor-injected). The asset system and catalog are
    // non-owning and optional: null until SetAssetEnvironment (brush-only).
    LoggingProvider& Logging;
    AssetSystem* Assets = nullptr;
    AssetRegistry* Catalog = nullptr;
};
