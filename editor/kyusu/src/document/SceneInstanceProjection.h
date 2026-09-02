#pragma once

#include "scene_source/SceneComposition.h"
#include "scene_source/SceneSourceCache.h"
#include "scene_source/SceneSourceDocument.h"

#include <core/json/JsonValue.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class AssetSystem;
class EditorScene;
class LoggingProvider;
class PersistentEntityIndex;
class Registry;
struct IComponentSerializer;
struct SceneSerializationContext;

// What resolution and expansion of a document's instances reported. Empty when
// everything resolved; the cook refuses on anything here, the editor shows it
// and keeps working.
struct SceneProjectionDiagnostics
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

//=============================================================================
// SceneInstanceProjection
//
// The two directions between a document's instance placement records and the
// live entities they stand for: expanding the records into derived entities,
// and folding what happened to those entities back into the records.
//
// It owns only the projection's own state -- where sources resolve, what is
// currently projected, what the last expansion reported, and who wants to know
// when it changes. The registry, the scene and the records it works on belong
// to the document and are named explicitly at construction; nothing here
// decides anything about the document itself, dirty state included.
//=============================================================================
class SceneInstanceProjection
{
public:
    SceneInstanceProjection(Registry& registry,
                            EditorScene& scene,
                            SceneSourceDocument& records,
                            LoggingProvider& logging);

    // Null until the document is given an asset environment, which is the
    // brush-only path: no asset fields to resolve.
    void SetAssets(AssetSystem* assets) { Assets = assets; }

    // Where asset://... source references resolve on disk.
    void SetContentRoots(std::vector<std::filesystem::path> roots);
    [[nodiscard]] const std::vector<std::filesystem::path>& ContentRoots() const
    {
        return Roots;
    }

    [[nodiscard]] const SceneProjectionDiagnostics& Diagnostics() const
    {
        return Diagnostics_;
    }

    // Fired after every rebuild, once the new projection is live. Projected
    // entities are destroyed and recreated, so anything holding their handles
    // -- the workspace's selection, above all -- has to re-resolve, and this is
    // how it learns without the projection knowing who is listening.
    void SetObserver(std::function<void()> observer)
    {
        Observer = std::move(observer);
    }

    // The lazily built source lookup over ContentRoots().
    [[nodiscard]] SceneSourceCache& Sources();

    // Destroys the derived entities and expands the records again.
    void Rebuild();

    // Folds what happened to the projected entities back into the records: the
    // root's transform into the placement, member edits into sparse patches,
    // added and removed components, entities added beneath the projection, and
    // deletions into suppressions. Runs before every save and every
    // re-projection, so the records are always the authority at rest.
    void Harvest();

    // Records fresh ids for every instance path resolution reported missing (a
    // source that grew since the placement was recorded). True when it minted,
    // which is what makes it an authored change to the document.
    [[nodiscard]] bool MintMissingIds();

    // Drops everything: no derived entities are destroyed, so this is for a
    // document that is being replaced wholesale.
    void Reset();

    [[nodiscard]] bool DependsOnSource(std::string_view assetPath) const;
    // Invalidates the named sources and re-projects if any of them mattered.
    bool ReloadSources(std::span<const std::string> assetPaths);

    [[nodiscard]] bool IsMember(EntityId entity) const;
    [[nodiscard]] bool IsRoot(EntityId entity) const;
    // The owning placement's id for a projected entity, root and added children
    // included; invalid outside any instance.
    [[nodiscard]] SceneInstanceId OwnerOf(EntityId entity) const;

    // The source's values for a projected member, as the shape a live entity
    // serializes to; null for a root, an added entity, or anything outside a
    // projection. This is what an override is measured against.
    [[nodiscard]] const Json5Value* BaselineOf(EntityId entity) const;
    [[nodiscard]] std::vector<std::byte> BaselineComponentBytes(
        EntityId entity, IComponentSerializer& serializer);

    // The entities a placement contributed stop being derived and become plain
    // locals; the live entities are untouched.
    void Forget(SceneInstanceId instance);

    // Entities the last harvest absorbed into add_entities records, so the
    // source build leaves them out of the local entity list.
    [[nodiscard]] const std::unordered_set<std::uint64_t>& AbsorbedIds() const
    {
        return AbsorbedIds_;
    }
    [[nodiscard]] bool Projects(PersistentEntityId id) const
    {
        return Elements.contains(id.Value);
    }

private:
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

    // The harvest's working state for one instance record, filled by the three
    // phases below in order.
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

    // The rebuild itself, with the many early exits its diagnostics need.
    // Rebuild wraps it so the announcement has one home.
    void Expand();
    [[nodiscard]] Json5Value SerializeSourceBaseline(
        const Json5Value& components, SceneSerializationContext& context);
    void HarvestProjectedElements(
        const PersistentEntityIndex& index,
        std::unordered_map<std::uint64_t, RecordHarvest>& byInstance);
    void AbsorbAuthoredChildren(
        std::unordered_map<std::uint64_t, RecordHarvest>& byInstance);
    void FoldHarvestsIntoRecords(
        const PersistentEntityIndex& index,
        std::unordered_map<std::uint64_t, RecordHarvest>& byInstance);

    Registry& Registry_;
    EditorScene& Scene;
    SceneSourceDocument& Records;
    LoggingProvider& Logging;
    AssetSystem* Assets = nullptr;

    std::vector<std::filesystem::path> Roots;
    std::unique_ptr<SceneSourceCache> Sources_;
    std::unordered_map<std::uint64_t, ProjectedElement> Elements;
    std::unordered_set<std::uint64_t> AbsorbedIds_;
    SceneProjectionDiagnostics Diagnostics_;
    std::function<void()> Observer;
};
