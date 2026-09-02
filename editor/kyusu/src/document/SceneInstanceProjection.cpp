#include "SceneInstanceProjection.h"

#include "EditorScene.h"
#include "scene_source/SceneSourcePaths.h"

#include "DocumentSerialization.h"
#include "EntityNameComponent.h"
#include "brush/BrushMeshSerialization.h"
#include "scene_source/Json5Convert.h"

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <cstring>
#include <core/serialization/JsonArchive.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace
{
    // Brush geometry is a per-document sidecar, so it never crosses documents
    // through the diff -- the projection copies the mesh in and rewrites the id,
    // and the harvest must not read that rewrite as an authored edit.
    [[nodiscard]] bool IsDocumentLocalComponent(std::string_view key)
    {
        return key == "brush" || key == "baked_brush";
    }

    // The sparse difference of two serializer-shaped objects: members that
    // changed, objects recursed into, anything else replaced whole. Null when
    // nothing differs.
    [[nodiscard]] Json5Value DiffValue(const Json5Value& live, const Json5Value& baseline)
    {
        if (live.IsObject() && baseline.IsObject())
        {
            Json5Value patch = Json5Value::MakeObject();
            for (const Json5Value::Member& member : live.Members)
            {
                const Json5Value* was = baseline.Find(member.first);
                if (was == nullptr)
                {
                    patch.Members.push_back(member);
                    continue;
                }
                Json5Value inner = DiffValue(member.second, *was);
                if (!inner.IsNull())
                    patch.Members.emplace_back(member.first, std::move(inner));
            }
            return patch.Members.empty() ? Json5Value{} : patch;
        }
        return live.SameValueAs(baseline) ? Json5Value{} : live;
    }

    // Strips the document-local components from a record destined for a
    // placement: brush geometry cannot cross documents by id.
    void StripDocumentLocal(Json5Value& components, Logger& log,
                            PersistentEntityId id)
    {
        const std::size_t before = components.Members.size();
        std::erase_if(components.Members, [](const Json5Value::Member& member)
                      { return IsDocumentLocalComponent(member.first); });
        if (components.Members.size() != before)
            log.Warn("scene projection: entity {} carries brush geometry the "
                     "placement record cannot hold; it stays editor-local",
                     PersistentEntityIdToString(id));
    }

    // The three ways one entity's components can differ from its baseline:
    // changed fields (a sparse patch), whole components the baseline lacked,
    // and baseline components the entity dropped. Document-local components
    // never cross into records.
    void DiffComponents(const Json5Value& live, const Json5Value& baseline,
                        Json5Value& patch, Json5Value& added,
                        std::vector<std::string>& removed)
    {
        for (const Json5Value::Member& member : live.Members)
        {
            if (IsDocumentLocalComponent(member.first))
                continue;
            const Json5Value* was = baseline.Find(member.first);
            if (was == nullptr)
            {
                added.Members.push_back(member);
                continue;
            }
            Json5Value diff = DiffValue(member.second, *was);
            if (!diff.IsNull())
                patch.Members.emplace_back(member.first, std::move(diff));
        }
        for (const Json5Value::Member& member : baseline.Members)
            if (!IsDocumentLocalComponent(member.first)
                && live.Find(member.first) == nullptr)
            {
                removed.push_back(member.first);
            }
    }

    [[nodiscard]] SceneElementPath InnerPath(const SceneElementPath& outer)
    {
        SceneElementPath inner;
        inner.Elements.assign(outer.Elements.begin() + 1, outer.Elements.end());
        return inner;
    }
} // namespace

SceneInstanceProjection::SceneInstanceProjection(Registry& registry,
                                                 EditorScene& scene,
                                                 SceneSourceDocument& records,
                                                 LoggingProvider& logging)
    : Registry_(registry)
    , Scene(scene)
    , Records(records)
    , Logging(logging)
{
}

void SceneInstanceProjection::Reset()
{
    Elements.clear();
    AbsorbedIds_.clear();
    Diagnostics_ = SceneProjectionDiagnostics{};
}

void SceneInstanceProjection::Forget(SceneInstanceId instance)
{
    std::erase_if(Elements, [&](const auto& entry)
                  { return entry.second.Path.Elements.front() == instance.Value; });
}

void SceneInstanceProjection::SetContentRoots(std::vector<std::filesystem::path> roots)
{
    Roots = std::move(roots);
    Sources_.reset();
}

SceneSourceCache& SceneInstanceProjection::Sources()
{
    if (Sources_ == nullptr)
        Sources_ = std::make_unique<SceneSourceCache>(Roots);
    return *Sources_;
}

void SceneInstanceProjection::Rebuild()
{
    Expand();

    // Every path through the expansion invalidated the previous projection's
    // handles, including the ones that expanded nothing, so the announcement
    // is unconditional.
    if (Observer)
        Observer();
}

// The source's values in the shape a live entity serializes to.
//
// An override is "how this entity differs from what its source says", so that
// is what the harvest has to diff against. Reading the baseline back off the
// instantiated entity would measure against the post-override values instead,
// and an override the load had already applied would look like no override at
// all -- which silently dropped it from the record on the next save.
//
// The values are materialized on a scratch entity and read back through the
// serializers, so both sides of the diff are serializer output rather than one
// side being file text. The scratch is never tracked and never identified, so
// it takes no part in the document beyond the moment it exists. Document-local
// components (brush geometry, which names sidecar ids) never enter a diff, so
// they are skipped.
Json5Value SceneInstanceProjection::SerializeSourceBaseline(
    const Json5Value& components, SceneSerializationContext& context)
{
    const EntityId scratch = Registry_.Components.CreateEntity();
    for (const Json5Value::Member& member : components.Members)
    {
        if (IsDocumentLocalComponent(member.first))
            continue;
        IComponentSerializer* serializer =
            EditorSceneSerializers().FindByJsonKey(member.first);
        if (serializer == nullptr)
            continue;
        serializer->RegisterStorage(Registry_);
        const JsonValue value = Json5ToJson(member.second);
        JsonReadArchive archive(value);
        (void)serializer->Load(archive, scratch, Registry_, context);
    }
    Json5Value baseline = SerializeEntityComponents(scratch, Registry_, context);
    Registry_.Components.DestroyEntity(scratch);
    return baseline;
}

void SceneInstanceProjection::Expand()
{
    // The records must already say everything the live projection knows,
    // because the projection is about to be destroyed.
    Harvest();

    // Leaf-up over everything the previous projection created.
    {
        std::vector<EntityId> doomed;
        if (const auto* index =
                Registry_.Components.TryGetResource<PersistentEntityIndex>())
        {
            for (const auto& [pid, element] : Elements)
            {
                const EntityId entity = index->TryResolve(PersistentEntityId{ pid });
                if (entity.IsValid())
                    doomed.push_back(entity);
            }
        }
        // Depth computed once per entity: the comparator runs O(n log n)
        // times and a chain walk per call turns the sort quadratic.
        std::vector<std::pair<std::size_t, EntityId>> byDepth;
        byDepth.reserve(doomed.size());
        for (EntityId entity : doomed)
        {
            std::size_t depth = 0;
            for (EntityId parent = Scene.GetParent(entity);
                 parent.IsValid() && depth < doomed.size() + 1;
                 parent = Scene.GetParent(parent))
                ++depth;
            byDepth.emplace_back(depth, entity);
        }
        std::sort(byDepth.begin(), byDepth.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [depth, entity] : byDepth)
            if (Scene.HasEntity(entity))
                Scene.DestroyEntity(entity);
    }
    Elements.clear();
    Diagnostics_ = SceneProjectionDiagnostics{};

    if (Records.Instances.empty())
        return;

    SceneSourceCache& sources = Sources();
    std::string resolveError;
    const std::optional<SceneCompositionResult> resolved =
        ResolveSceneComposition(Records, sources, &resolveError);
    if (!resolved.has_value())
    {
        if (!sources.LastError().empty())
            resolveError += " (" + sources.LastError() + ")";
        Diagnostics_.ResolveError = resolveError;
        Logging.GetLogger<SceneInstanceProjection>().Error(
            "scene projection: {}", resolveError);
        return;
    }

    for (const auto& [instance, path] : resolved->MissingIds)
        Diagnostics_.MissingIds.push_back(
            PersistentEntityIdToString(PersistentEntityId{ instance.Value })
            + ": " + path.ToString());
    Diagnostics_.DanglingOverrides = resolved->DanglingOverrides;

    // Instantiate the expanded entities -- everything the resolver produced
    // beyond the document's own locals, which are already live.
    SceneSerializationContext context(Logging, Assets);
    const auto* index = Registry_.Components.TryGetResource<PersistentEntityIndex>();
    Logger& log = Logging.GetLogger<SceneInstanceProjection>();

    for (const ResolvedSceneEntity& element : resolved->Entities)
    {
        if (!element.Instance.IsValid())
            continue; // a local: already live

        const EntityId entity = Registry_.Components.CreateEntity();
        Registry_.Components.AddComponent(entity,
                                          PersistentIdComponent{ element.Id });

        for (const Json5Value::Member& member : element.Components.Members)
        {
            if (member.first == "persistent_id")
                continue;

            // Brush geometry: copy the mesh out of the defining document's
            // sidecar into this one under a fresh id, then point the component
            // at the copy.
            if (IsDocumentLocalComponent(member.first))
            {
                if (element.SourceBrushMeshes == nullptr
                    || !element.SourceBrushMeshes->IsObject())
                {
                    continue;
                }
                const Json5Value* id = member.second.Find("id");
                const Json5Value* source = member.second.Find("source");
                const Json5Value* which = id != nullptr ? id : source;
                if (which == nullptr || !which->IsNumber())
                    continue;
                const std::string meshKey =
                    std::to_string(static_cast<std::uint32_t>(which->Number));
                const Json5Value* mesh = element.SourceBrushMeshes->Find(meshKey);
                if (mesh == nullptr)
                {
                    log.Warn("scene projection: instance brush mesh '{}' missing "
                             "from its source sidecar", meshKey);
                    continue;
                }
                const BrushId copied = Scene.GetBrushMeshStore().Create(
                    BrushMeshFromJson(Json5ToJson(*mesh)));
                if (member.first == "brush")
                    Registry_.Components.AddComponent(entity, BrushComponent{ copied });
                else
                    Registry_.Components.AddComponent(entity,
                                                      BakedBrushComponent{ copied });
                continue;
            }

            IComponentSerializer* serializer =
                EditorSceneSerializers().FindByJsonKey(member.first);
            if (serializer == nullptr)
                continue; // unknown to this build; the record still holds it
            serializer->RegisterStorage(Registry_);
            const JsonValue value = Json5ToJson(member.second);
            JsonReadArchive archive(value);
            if (!serializer->Load(archive, entity, Registry_, context))
                log.Warn("scene projection: component '{}' on '{}' failed to load",
                         member.first, element.Path.ToString());
        }

        Scene.TrackEntity(entity);
        Scene.SetEntityVisible(entity, !element.Hidden);
        Scene.SetEntityLocked(entity, element.Locked);

        ProjectedElement record;
        record.Path = element.Path;
        record.Instance = element.Instance;
        record.Root = element.IsInstanceRoot;
        record.Added = element.IsAdded;
        record.Baseline = SerializeSourceBaseline(element.SourceComponents, context);
        Elements.emplace(element.Id.Value, std::move(record));
    }

    // Parentage second, everything now resolvable: expanded entities to their
    // parents, and locals whose records name an instance root that did not
    // exist during the source load.
    if (index != nullptr)
    {
        for (const ResolvedSceneEntity& element : resolved->Entities)
        {
            if (!element.Parent.IsValid())
                continue;
            const EntityId child = index->TryResolve(element.Id);
            const EntityId parent = index->TryResolve(element.Parent);
            if (child.IsValid() && parent.IsValid()
                && Scene.GetParent(child) != parent)
            {
                (void)Scene.SetParent(child, parent);
            }
        }
    }
}

bool SceneInstanceProjection::MintMissingIds()
{
    std::string resolveError;
    const std::optional<SceneCompositionResult> resolved =
        ResolveSceneComposition(Records, Sources(), &resolveError);
    if (!resolved.has_value() || resolved->MissingIds.empty())
        return false;

    for (const auto& [instanceId, path] : resolved->MissingIds)
    {
        for (SceneInstanceRecord& record : Records.Instances)
        {
            if (record.Id != instanceId)
                continue;
            const bool recorded = std::any_of(
                record.EntityIds.begin(), record.EntityIds.end(),
                [&](const auto& entry) { return entry.first == path; });
            if (!recorded)
                record.EntityIds.emplace_back(path, Scene.MintPersistentId());
            break;
        }
    }

    Rebuild();
    return true;
}

const Json5Value* SceneInstanceProjection::BaselineOf(EntityId entity) const
{
    const auto* id = Registry_.Components.TryGet<PersistentIdComponent>(entity);
    if (id == nullptr)
        return nullptr;
    const auto found = Elements.find(id->Id.Value);
    if (found == Elements.end() || found->second.Root || found->second.Added)
        return nullptr;
    return &found->second.Baseline;
}

std::vector<std::byte> SceneInstanceProjection::BaselineComponentBytes(
    EntityId entity, IComponentSerializer& serializer)
{
    const Json5Value* baseline = BaselineOf(entity);
    if (baseline == nullptr)
        return {};
    const Json5Value* component = baseline->Find(serializer.JsonKey());
    if (component == nullptr)
        return {};

    // Materialize on a scratch entity so the bytes come out of the same
    // serializer path the live entity's went in through; never tracked, so
    // it takes no part in the document beyond this call.
    SceneSerializationContext context(Logging, Assets);
    const EntityId scratch = Registry_.Components.CreateEntity();
    serializer.RegisterStorage(Registry_);
    const JsonValue value = Json5ToJson(*component);
    JsonReadArchive archive(value);
    std::vector<std::byte> bytes;
    if (serializer.Load(archive, scratch, Registry_, context))
    {
        const ComponentId id =
            Registry_.Components.GetComponentIdByType(serializer.TypeId());
        const ComponentMeta* meta = Registry_.Components.GetMeta(id);
        if (const void* raw = Registry_.Components.GetComponentRaw(scratch, id);
            raw != nullptr && meta != nullptr && meta->Size > 0)
        {
            bytes.resize(meta->Size);
            std::memcpy(bytes.data(), raw, meta->Size);
        }
    }
    Registry_.Components.DestroyEntity(scratch);
    return bytes;
}

bool SceneInstanceProjection::DependsOnSource(std::string_view assetPath) const
{
    for (const SceneInstanceRecord& record : Records.Instances)
        if (record.Source == assetPath)
            return true;
    return Sources_ != nullptr && Sources_->HasLoaded(assetPath);
}

bool SceneInstanceProjection::ReloadSources(std::span<const std::string> assetPaths)
{
    bool any = false;
    for (const std::string& assetPath : assetPaths)
    {
        if (!DependsOnSource(assetPath))
            continue;
        if (Sources_ != nullptr)
            Sources_->Invalidate(assetPath);
        any = true;
    }
    if (any)
        Rebuild();
    return any;
}

void SceneInstanceProjection::Harvest()
{
    AbsorbedIds_.clear();
    if (Elements.empty())
        return;

    const auto* index = Registry_.Components.TryGetResource<PersistentEntityIndex>();
    if (index == nullptr)
        return;

    std::unordered_map<std::uint64_t, RecordHarvest> byInstance;
    for (const SceneInstanceRecord& record : Records.Instances)
        byInstance.emplace(record.Id.Value, RecordHarvest{});

    HarvestProjectedElements(*index, byInstance);
    AbsorbAuthoredChildren(byInstance);
    FoldHarvestsIntoRecords(*index, byInstance);
}

// Phase one: every element the projection produced lands in its instance's
// harvest bucket -- the root's liveness, an added entity's whole record, a
// member's sparse diff against its baseline, or a suppression.
void SceneInstanceProjection::HarvestProjectedElements(
    const PersistentEntityIndex& index,
    std::unordered_map<std::uint64_t, RecordHarvest>& byInstance)
{
    SceneSerializationContext context(Logging, Assets);
    // Sorted by projected path: this loop's append order becomes the saved
    // file's patch/added/removed order, and unordered_map bucket order would
    // reshuffle those blocks whenever the projection rehashes.
    std::vector<std::pair<const std::uint64_t, ProjectedElement>*> ordered;
    ordered.reserve(Elements.size());
    for (auto& entry : Elements)
        ordered.push_back(&entry);
    std::sort(ordered.begin(), ordered.end(),
              [](const auto* a, const auto* b)
              { return a->second.Path.Elements < b->second.Path.Elements; });

    for (const auto* entry : ordered)
    {
        const std::uint64_t pid = entry->first;
        const ProjectedElement& element = entry->second;
        const std::uint64_t owner = element.Path.Elements.front();
        const auto harvest = byInstance.find(owner);
        if (harvest == byInstance.end())
            continue;

        const EntityId entity = index.TryResolve(PersistentEntityId{ pid });
        const bool alive = entity.IsValid() && Scene.HasEntity(entity);

        if (element.Root)
        {
            harvest->second.RootSeen = true;
            harvest->second.RootAlive = alive;
            continue;
        }

        // The placement's own addition: its whole live state IS the record,
        // and deleting it deletes the record rather than suppressing a source
        // entity that never existed.
        if (element.Added)
        {
            if (!alive)
                continue;
            SceneAddedEntity record;
            record.Id = PersistentEntityId{ pid };
            if (const EntityId parent = Scene.GetParent(entity); parent.IsValid())
                if (const auto* parentId =
                        Registry_.Components.TryGet<PersistentIdComponent>(parent))
                    if (const auto projectedParent =
                            Elements.find(parentId->Id.Value);
                        projectedParent != Elements.end()
                        && !projectedParent->second.Root)
                    {
                        record.ParentPath = InnerPath(projectedParent->second.Path);
                    }
            record.Components = SerializeEntityComponents(entity, Registry_, context);
            StripDocumentLocal(record.Components,
                               Logging.GetLogger<SceneInstanceProjection>(), record.Id);
            harvest->second.AddedEntities.push_back(std::move(record));
            continue;
        }

        if (!alive)
        {
            harvest->second.Suppressed.push_back(InnerPath(element.Path));
            continue;
        }

        const Json5Value live = SerializeEntityComponents(entity, Registry_, context);
        Json5Value patch = Json5Value::MakeObject();
        Json5Value added = Json5Value::MakeObject();
        std::vector<std::string> removed;
        DiffComponents(live, element.Baseline, patch, added, removed);

        const SceneElementPath inner = InnerPath(element.Path);
        if (!patch.Members.empty())
            harvest->second.Patches.emplace_back(inner, std::move(patch));
        if (!added.Members.empty())
            harvest->second.Added.emplace_back(inner, std::move(added));
        if (!removed.empty())
            harvest->second.Removed.emplace_back(inner, std::move(removed));
    }
}

// Phase two: entities authored beneath the projection. A live local whose
// ancestry reaches a projected entity belongs to the placement as an added
// entity, not to the document's local list -- a local record cannot legally
// name a projected parent. Deeper authored trees inside an instance flatten
// onto it: an added-entity record can only name a source path as its parent.
void SceneInstanceProjection::AbsorbAuthoredChildren(
    std::unordered_map<std::uint64_t, RecordHarvest>& byInstance)
{
    SceneSerializationContext context(Logging, Assets);
    const World& world = Registry_.Components;
    Logger& log = Logging.GetLogger<SceneInstanceProjection>();
    for (EntityId entity : Scene.GetAllEntities())
    {
        const auto* id = world.TryGet<PersistentIdComponent>(entity);
        if (id == nullptr || Elements.contains(id->Id.Value))
            continue;

        const ProjectedElement* anchor = nullptr;
        bool crossedLocal = false;
        std::size_t hops = 0;
        for (EntityId parent = Scene.GetParent(entity);
             parent.IsValid() && anchor == nullptr && hops < 256;
             parent = Scene.GetParent(parent), ++hops)
        {
            const auto* parentId = world.TryGet<PersistentIdComponent>(parent);
            if (parentId == nullptr)
                break;
            const auto projected = Elements.find(parentId->Id.Value);
            if (projected != Elements.end())
                anchor = &projected->second;
            else
                crossedLocal = true;
        }
        if (anchor == nullptr)
            continue;
        if (crossedLocal)
            log.Warn("scene projection: entity {} sits below another authored "
                     "entity inside an instance; the placement records it "
                     "directly beneath the instance's own entity",
                     PersistentEntityIdToString(id->Id));

        const std::uint64_t owner = anchor->Path.Elements.front();
        const auto harvest = byInstance.find(owner);
        if (harvest == byInstance.end())
            continue;

        SceneAddedEntity record;
        record.Id = id->Id;
        record.ParentPath = anchor->Root ? SceneElementPath{}
                                         : InnerPath(anchor->Path);
        record.Components = SerializeEntityComponents(entity, Registry_, context);
        StripDocumentLocal(record.Components, log, record.Id);
        harvest->second.AddedEntities.push_back(std::move(record));
        AbsorbedIds_.insert(id->Id.Value);
    }
}

// Phase three: fold each harvest into its record. A record the projection
// never produced a root for is not the harvest's to judge; a dead projected
// root means the placement was deleted. Entries whose paths this projection
// did not produce -- dangling, possibly meaningful to a build that resolves
// more -- carry over rather than evaporating.
void SceneInstanceProjection::FoldHarvestsIntoRecords(
    const PersistentEntityIndex& index,
    std::unordered_map<std::uint64_t, RecordHarvest>& byInstance)
{
    // Each record probes this once per stored override, so the projection is
    // indexed up front instead of rescanned (and re-allocated) per probe.
    std::unordered_map<std::uint64_t, std::vector<std::vector<std::uint64_t>>>
        projectedInner;
    for (const auto& [pid, element] : Elements)
        if (!element.Root)
            projectedInner[element.Path.Elements.front()].push_back(
                InnerPath(element.Path).Elements);
    for (auto& [instance, paths] : projectedInner)
        std::sort(paths.begin(), paths.end());
    const auto isProjectedPathOf = [&](std::uint64_t instance,
                                       const SceneElementPath& inner)
    {
        const auto found = projectedInner.find(instance);
        return found != projectedInner.end()
            && std::binary_search(found->second.begin(), found->second.end(),
                                  inner.Elements);
    };

    std::erase_if(Records.Instances, [&](SceneInstanceRecord& record)
    {
        const auto harvest = byInstance.find(record.Id.Value);
        if (harvest == byInstance.end())
            return false;
        if (!harvest->second.RootSeen)
            return false; // never projected: nothing to fold, nothing to judge
        if (!harvest->second.RootAlive)
            return true; // the placement itself was deleted

        // The root's live transform is the placement, its live parent is the
        // record's parent, and its authored name is the record's name.
        if (const EntityId root =
                index.TryResolve(PersistentEntityId{ record.Id.Value });
            root.IsValid())
        {
            if (const Transform3f* local = Scene.TryGetLocalTransform(root))
                record.Placement = *local;
            record.Name.clear();
            if (const auto* name =
                    Registry_.Components.TryGet<EntityNameComponent>(root);
                name != nullptr)
            {
                record.Name = std::string(name->Value.View());
            }
            record.Parent = PersistentEntityId{};
            if (const EntityId parent = Scene.GetParent(root); parent.IsValid())
                if (const auto* parentId =
                        Registry_.Components.TryGet<PersistentIdComponent>(parent))
                    record.Parent = parentId->Id;
        }

        RecordHarvest& fresh = harvest->second;
        const auto carryDangling = [&](auto& stored, auto& rebuilt)
        {
            for (auto& entry : stored)
                if (!isProjectedPathOf(record.Id.Value, entry.first))
                    rebuilt.push_back(std::move(entry));
            stored = std::move(rebuilt);
        };
        carryDangling(record.Patches, fresh.Patches);
        carryDangling(record.AddedComponents, fresh.Added);
        carryDangling(record.RemovedComponents, fresh.Removed);

        // A suppression's target is, by construction, not in the projection
        // (the resolver dropped it), so prior suppressions carry unless their
        // entity came back -- an undo of the delete re-projects it next load,
        // and its liveness this session is what fresh recorded.
        for (SceneElementPath& prior : record.Suppressed)
        {
            const bool freshHas = std::any_of(
                fresh.Suppressed.begin(), fresh.Suppressed.end(),
                [&](const SceneElementPath& path) { return path == prior; });
            if (!freshHas && !isProjectedPathOf(record.Id.Value, prior))
                fresh.Suppressed.push_back(std::move(prior));
        }
        record.Suppressed = std::move(fresh.Suppressed);

        // Added-entity records whose entities this projection produced were
        // re-harvested in phase one; records the projection never saw carry.
        for (SceneAddedEntity& prior : record.AddedEntities)
            if (!Elements.contains(prior.Id.Value))
                fresh.AddedEntities.push_back(std::move(prior));
        record.AddedEntities = std::move(fresh.AddedEntities);
        return false;
    });
}

bool SceneInstanceProjection::IsMember(EntityId entity) const
{
    const auto* id = Registry_.Components.TryGet<PersistentIdComponent>(entity);
    if (id == nullptr)
        return false;
    const auto found = Elements.find(id->Id.Value);
    return found != Elements.end() && !found->second.Root;
}

bool SceneInstanceProjection::IsRoot(EntityId entity) const
{
    const auto* id = Registry_.Components.TryGet<PersistentIdComponent>(entity);
    if (id == nullptr)
        return false;
    const auto found = Elements.find(id->Id.Value);
    return found != Elements.end() && found->second.Root;
}

SceneInstanceId SceneInstanceProjection::OwnerOf(EntityId entity) const
{
    const auto* id = Registry_.Components.TryGet<PersistentIdComponent>(entity);
    if (id == nullptr)
        return {};
    const auto found = Elements.find(id->Id.Value);
    if (found == Elements.end() || found->second.Path.Elements.empty())
        return {};
    return SceneInstanceId{ found->second.Path.Elements.front() };
}