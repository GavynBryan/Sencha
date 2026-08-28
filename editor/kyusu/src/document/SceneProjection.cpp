// The document's instance projection: expanding placement records into derived
// live entities, and folding what happened to them back into the records.
// EditorDocument's other mechanisms live in EditorDocument.cpp; this file is
// the projection alone.

#include "EditorDocument.h"

#include "DocumentSerialization.h"
#include "EntityNameComponent.h"
#include "brush/BrushMeshSerialization.h"
#include "brush/BrushValidation.h"
#include "scene_source/Json5Convert.h"

#include <core/logging/Logger.h>
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

    // The entity's components as the serializers say they are right now --
    // the same shape the baseline was captured in, so the diff is edits only.
    [[nodiscard]] Json5Value SerializeLiveComponents(
        const Registry& registry, EntityId entity,
        LoggingProvider& logging, AssetSystem* assets)
    {
        SceneSerializationContext context(logging, assets);
        Json5Value components = Json5Value::MakeObject();
        for (const auto& serializer : EditorSceneSerializers().Entries())
        {
            if (serializer->JsonKey() == "persistent_id")
                continue;
            if (!serializer->HasComponent(entity, registry))
                continue;
            JsonWriteArchive archive;
            if (!serializer->Save(archive, entity, registry, context) || !archive.Ok())
                continue;
            JsonValue value = archive.TakeValue();
            if (!value.IsNull())
                components.Members.emplace_back(std::string(serializer->JsonKey()),
                                                Json5FromJson(value));
        }
        return components;
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

    [[nodiscard]] SceneElementPath InnerPath(const SceneElementPath& outer)
    {
        SceneElementPath inner;
        inner.Elements.assign(outer.Elements.begin() + 1, outer.Elements.end());
        return inner;
    }
} // namespace

void EditorDocument::SetContentRoots(std::vector<std::filesystem::path> roots)
{
    ContentRoots_ = std::move(roots);
    SourceCache_.reset();
}

void EditorDocument::RebuildSceneProjection()
{
    // The records must already say everything the live projection knows,
    // because the projection is about to be destroyed.
    HarvestInstanceOverrides();

    // Leaf-up over everything the previous projection created.
    {
        std::vector<EntityId> doomed;
        if (const auto* index =
                Registry_.Components.TryGetResource<PersistentEntityIndex>())
        {
            for (const auto& [pid, element] : Projection_)
            {
                const EntityId entity = index->TryResolve(PersistentEntityId{ pid });
                if (entity.IsValid())
                    doomed.push_back(entity);
            }
        }
        const auto depthOf = [&](EntityId entity)
        {
            std::size_t depth = 0;
            for (EntityId parent = Scene.GetParent(entity);
                 parent.IsValid() && depth < doomed.size() + 1;
                 parent = Scene.GetParent(parent))
                ++depth;
            return depth;
        };
        std::sort(doomed.begin(), doomed.end(),
                  [&](EntityId a, EntityId b) { return depthOf(a) > depthOf(b); });
        for (EntityId entity : doomed)
            if (Scene.HasEntity(entity))
                Scene.DestroyEntity(entity);
    }
    Projection_.clear();
    ProjectionDiagnostics_ = ProjectionDiagnostics{};

    if (Retained_.Instances.empty())
        return;

    if (SourceCache_ == nullptr)
        SourceCache_ = std::make_unique<SceneSourceCache>(ContentRoots_);

    std::string resolveError;
    const std::optional<SceneCompositionResult> resolved =
        ResolveSceneComposition(Retained_, *SourceCache_, &resolveError);
    if (!resolved.has_value())
    {
        if (!SourceCache_->LastError().empty())
            resolveError += " (" + SourceCache_->LastError() + ")";
        ProjectionDiagnostics_.ResolveError = resolveError;
        Logging.GetLogger<EditorDocument>().Error(
            "scene projection: {}", resolveError);
        return;
    }

    for (const auto& [instance, path] : resolved->MissingIds)
        ProjectionDiagnostics_.MissingIds.push_back(
            PersistentEntityIdToString(PersistentEntityId{ instance.Value })
            + ": " + path.ToString());
    ProjectionDiagnostics_.DanglingOverrides = resolved->DanglingOverrides;

    // Instantiate the expanded entities -- everything the resolver produced
    // beyond the document's own locals, which are already live.
    SceneSerializationContext context(Logging, Assets);
    const auto* index = Registry_.Components.TryGetResource<PersistentEntityIndex>();
    Logger& log = Logging.GetLogger<EditorDocument>();

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
                BrushMesh copy = BrushMeshFromJson(Json5ToJson(*mesh));
                // The repair every serialized mesh gets on entry: face normals
                // are cached members the tessellator reads, and a parse alone
                // leaves them zero -- which lights as black, not as an error.
                BrushValidateAndRepair(copy);
                const BrushId copied =
                    Scene.GetBrushMeshStore().Create(std::move(copy));
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
        record.Baseline = SerializeLiveComponents(Registry_, entity, Logging, Assets);
        Projection_.emplace(element.Id.Value, std::move(record));
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

void EditorDocument::MintMissingInstanceIds()
{
    if (SourceCache_ == nullptr)
        SourceCache_ = std::make_unique<SceneSourceCache>(ContentRoots_);

    std::string resolveError;
    const std::optional<SceneCompositionResult> resolved =
        ResolveSceneComposition(Retained_, *SourceCache_, &resolveError);
    if (!resolved.has_value() || resolved->MissingIds.empty())
        return;

    for (const auto& [instanceId, path] : resolved->MissingIds)
    {
        for (SceneInstanceRecord& record : Retained_.Instances)
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

    MarkDirty();
    RebuildSceneProjection();
}

void EditorDocument::HarvestInstanceOverrides()
{
    AbsorbedPids_.clear();
    if (Projection_.empty())
        return;

    const auto* index = Registry_.Components.TryGetResource<PersistentEntityIndex>();
    if (index == nullptr)
        return;

    // Fresh override state per record, with entries whose paths the projection
    // never produced -- dangling, possibly meaningful to a build that can
    // resolve more than this one -- carried over rather than dropped.
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
        // speak about what the projection produced; a record it never saw is
        // not its to judge.
        bool RootSeen = false;
        bool RootAlive = false;
    };
    std::unordered_map<std::uint64_t, RecordHarvest> byInstance;
    for (const SceneInstanceRecord& record : Retained_.Instances)
        byInstance.emplace(record.Id.Value, RecordHarvest{});

    for (const auto& [pid, element] : Projection_)
    {
        const std::uint64_t owner = element.Path.Elements.front();
        const auto harvest = byInstance.find(owner);
        if (harvest == byInstance.end())
            continue;

        const EntityId entity = index->TryResolve(PersistentEntityId{ pid });
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
                            Projection_.find(parentId->Id.Value);
                        projectedParent != Projection_.end()
                        && !projectedParent->second.Root)
                    {
                        record.ParentPath = InnerPath(projectedParent->second.Path);
                    }
            record.Components =
                SerializeLiveComponents(Registry_, entity, Logging, Assets);
            StripDocumentLocal(record.Components,
                               Logging.GetLogger<EditorDocument>(), record.Id);
            harvest->second.AddedEntities.push_back(std::move(record));
            continue;
        }

        if (!alive)
        {
            harvest->second.Suppressed.push_back(InnerPath(element.Path));
            continue;
        }

        const Json5Value live =
            SerializeLiveComponents(Registry_, entity, Logging, Assets);

        Json5Value patch = Json5Value::MakeObject();
        Json5Value added = Json5Value::MakeObject();
        std::vector<std::string> removed;
        for (const Json5Value::Member& member : live.Members)
        {
            if (IsDocumentLocalComponent(member.first))
                continue;
            const Json5Value* was = element.Baseline.Find(member.first);
            if (was == nullptr)
            {
                added.Members.push_back(member);
                continue;
            }
            Json5Value diff = DiffValue(member.second, *was);
            if (!diff.IsNull())
                patch.Members.emplace_back(member.first, std::move(diff));
        }
        for (const Json5Value::Member& member : element.Baseline.Members)
            if (!IsDocumentLocalComponent(member.first)
                && live.Find(member.first) == nullptr)
            {
                removed.push_back(member.first);
            }

        const SceneElementPath inner = InnerPath(element.Path);
        if (!patch.Members.empty())
            harvest->second.Patches.emplace_back(inner, std::move(patch));
        if (!added.Members.empty())
            harvest->second.Added.emplace_back(inner, std::move(added));
        if (!removed.empty())
            harvest->second.Removed.emplace_back(inner, std::move(removed));
    }

    // Entities authored beneath the projection: a live local whose parent is a
    // projected entity belongs to the placement as an added entity, not to the
    // document's local list -- a local record cannot legally name a projected
    // parent.
    const World& world = Registry_.Components;
    Logger& log = Logging.GetLogger<EditorDocument>();
    for (EntityId entity : Scene.GetAllEntities())
    {
        const auto* id = world.TryGet<PersistentIdComponent>(entity);
        if (id == nullptr || Projection_.contains(id->Id.Value))
            continue;

        // Walk to the nearest projected ancestor. Deeper authored trees inside
        // an instance flatten onto it: an added-entity record can only name a
        // source path as its parent.
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
            const auto projected = Projection_.find(parentId->Id.Value);
            if (projected != Projection_.end())
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
        record.Components = SerializeLiveComponents(Registry_, entity, Logging, Assets);
        StripDocumentLocal(record.Components, log, record.Id);
        harvest->second.AddedEntities.push_back(std::move(record));
        AbsorbedPids_.insert(id->Id.Value);
    }

    // Fold the harvests into the records. A dead root means the placement was
    // deleted; carried dangling entries are the ones whose paths this
    // projection did not produce.
    const auto isProjectedPathOf = [&](std::uint64_t instance,
                                       const SceneElementPath& inner)
    {
        for (const auto& [pid, element] : Projection_)
        {
            if (element.Root || element.Path.Elements.front() != instance)
                continue;
            if (InnerPath(element.Path) == inner)
                return true;
        }
        return false;
    };

    std::erase_if(Retained_.Instances, [&](SceneInstanceRecord& record)
    {
        const auto harvest = byInstance.find(record.Id.Value);
        if (harvest == byInstance.end())
            return false;
        if (!harvest->second.RootSeen)
            return false; // never projected: nothing to fold, nothing to judge
        if (!harvest->second.RootAlive)
            return true; // the placement itself was deleted

        // The root's live transform is the placement, and its live parent is
        // the record's parent -- reparenting a placement is an ordinary
        // hierarchy edit on its root.
        if (const EntityId root =
                index->TryResolve(PersistentEntityId{ record.Id.Value });
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
        // re-harvested above; records the projection never saw carry as-is.
        for (SceneAddedEntity& prior : record.AddedEntities)
            if (!Projection_.contains(prior.Id.Value))
                fresh.AddedEntities.push_back(std::move(prior));
        record.AddedEntities = std::move(fresh.AddedEntities);
        return false;
    });
}

SceneInstanceId EditorDocument::PlaceSceneInstance(std::string source,
                                                   const Transform3f& placement,
                                                   PersistentEntityId parent,
                                                   std::string* error)
{
    if (SourceCache_ == nullptr)
        SourceCache_ = std::make_unique<SceneSourceCache>(ContentRoots_);
    if (SourceCache_->Find(source) == nullptr)
    {
        if (error != nullptr)
            *error = SourceCache_->LastError();
        return SceneInstanceId{};
    }

    const SceneInstanceId id{ Scene.MintPersistentId().Value };
    SceneInstanceRecord record;
    record.Id = id;
    record.Parent = parent;
    record.Source = std::move(source);
    record.Placement = placement;
    Retained_.Instances.push_back(std::move(record));

    // Mints an id for every path the source contributes and re-projects; also
    // the authoring act that marks the document dirty.
    MintMissingInstanceIds();

    // By id, never by position: the mint's rebuild harvests, and the record's
    // survival is the placement's success.
    if (FindSceneInstance(id) == nullptr)
    {
        if (error != nullptr)
            *error = "the placement did not survive projection";
        return SceneInstanceId{};
    }
    return id;
}

const SceneInstanceRecord* EditorDocument::FindSceneInstance(SceneInstanceId id) const
{
    for (const SceneInstanceRecord& record : Retained_.Instances)
        if (record.Id == id)
            return &record;
    return nullptr;
}

bool EditorDocument::RestoreSceneInstance(SceneInstanceRecord record)
{
    if (FindSceneInstance(record.Id) != nullptr)
        return false;
    Retained_.Instances.push_back(std::move(record));
    MarkDirty();
    RebuildSceneProjection();
    return true;
}

bool EditorDocument::RemoveSceneInstance(SceneInstanceId id, SceneInstanceRecord* removed)
{
    // Harvest first so the captured record carries every unsaved edit; that is
    // what makes an undo of the removal restore the placement as it was.
    HarvestInstanceOverrides();
    const auto found = std::find_if(Retained_.Instances.begin(),
                                    Retained_.Instances.end(),
                                    [&](const SceneInstanceRecord& record)
                                    { return record.Id == id; });
    if (found == Retained_.Instances.end())
        return false;
    if (removed != nullptr)
        *removed = *found;
    Retained_.Instances.erase(found);
    MarkDirty();
    RebuildSceneProjection();
    return true;
}

bool EditorDocument::BreakSceneInstance(SceneInstanceId id, SceneInstanceRecord* broken)
{
    // The record must say everything the live entities do, because undo will
    // rebuild the placement from it.
    HarvestInstanceOverrides();
    const auto found = std::find_if(Retained_.Instances.begin(),
                                    Retained_.Instances.end(),
                                    [&](const SceneInstanceRecord& record)
                                    { return record.Id == id; });
    if (found == Retained_.Instances.end())
        return false;
    if (broken != nullptr)
        *broken = *found;

    // The live entities stay exactly as they are; they simply stop being a
    // projection. Everything the placement contributed -- nested content
    // included -- becomes plain local entities of this document.
    std::erase_if(Projection_, [&](const auto& entry)
                  { return entry.second.Path.Elements.front() == id.Value; });
    Retained_.Instances.erase(found);
    MarkDirty();
    return true;
}

bool EditorDocument::IsSceneInstanceMember(EntityId entity) const
{
    const auto* id = Registry_.Components.TryGet<PersistentIdComponent>(entity);
    if (id == nullptr)
        return false;
    const auto found = Projection_.find(id->Id.Value);
    return found != Projection_.end() && !found->second.Root;
}

bool EditorDocument::IsSceneInstanceRoot(EntityId entity) const
{
    const auto* id = Registry_.Components.TryGet<PersistentIdComponent>(entity);
    if (id == nullptr)
        return false;
    const auto found = Projection_.find(id->Id.Value);
    return found != Projection_.end() && found->second.Root;
}

std::string EditorDocument::SceneInstanceSourceOf(EntityId entity) const
{
    const auto* id = Registry_.Components.TryGet<PersistentIdComponent>(entity);
    if (id == nullptr)
        return {};
    const auto found = Projection_.find(id->Id.Value);
    if (found == Projection_.end())
        return {};
    const SceneInstanceRecord* record = FindSceneInstance(
        SceneInstanceId{ found->second.Path.Elements.front() });
    return record != nullptr ? record->Source : std::string{};
}
