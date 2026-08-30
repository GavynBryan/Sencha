#include "EditorDocument.h"

#include "brush/BrushMeshSerialization.h"
#include "EntityNameComponent.h"

#include "scene_source/Json5Convert.h"

#include <core/assets/AssetRegistry.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/JsonArchive.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <render/extract/Camera.h>
#include <render/StaticMeshComponent.h>
#include <anim/AnimationClipPlayerComponent.h>
#include <render/skinned_mesh/SkinnedMeshComponent.h>
#include <movement/MovementRegistration.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>
#include <world/ComponentRegistrar.h>
#include <world/WorldComponentRegistration.h>
#include <world/transform/TransformComponents.h>

#include <fstream>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "DocumentSerialization.h"

namespace
{
    // Collects every asset:// string anywhere in a component subtree,
    // shape-agnostic: a bare path value or a path nested in an {id, path} ref
    // both surface, with no per-type knowledge.
    void CollectAssetRefs5(const Json5Value& value, std::vector<std::string>& out)
    {
        if (value.IsString())
        {
            if (value.Text.rfind("asset://", 0) == 0)
                out.push_back(value.Text);
        }
        else if (value.IsObject())
        {
            for (const Json5Value::Member& member : value.Members)
                CollectAssetRefs5(member.second, out);
        }
        else if (value.IsArray())
        {
            for (const Json5Value& element : value.Elements)
                CollectAssetRefs5(element, out);
        }
    }

    // Comments matched onto the regenerated values by member name, recursively.
    // Values are entirely the fresh side's; only trivia crosses.
    void CarryTrivia(Json5Value& fresh, const Json5Value& retained)
    {
        fresh.LeadingComments = retained.LeadingComments;
        fresh.TrailingComments = retained.TrailingComments;
        if (fresh.IsObject() && retained.IsObject())
            for (Json5Value::Member& member : fresh.Members)
                if (const Json5Value* match = retained.Find(member.first))
                    CarryTrivia(member.second, *match);
    }

    // The retention contract for one entity's components on save. Fresh values
    // are authoritative for everything a serializer owns; the retained subtree
    // contributes comments everywhere, unknown components wholesale, and
    // unknown top-level fields inside known components. A retained member
    // whose name a serializer DOES own but which is absent from fresh was
    // removed or omitted by the serializer, and resurrecting its stale value
    // would hand the document a second authority -- it is dropped.
    void MergeRetainedComponents(Json5Value& fresh, const Json5Value& retained)
    {
        if (!retained.IsObject())
            return;

        for (Json5Value::Member& member : fresh.Members)
        {
            const Json5Value* match = retained.Find(member.first);
            if (match == nullptr)
                continue;
            CarryTrivia(member.second, *match);

            // Unknown top-level fields of a known component ride along.
            const IComponentSerializer* serializer =
                EditorSceneSerializers().FindByJsonKey(member.first);
            if (serializer == nullptr || !member.second.IsObject()
                || !match->IsObject())
            {
                continue;
            }
            const std::span<const RuntimeField> fields = serializer->RuntimeFields();
            for (const Json5Value::Member& retainedField : match->Members)
            {
                if (member.second.Find(retainedField.first) != nullptr)
                    continue;
                const bool known = std::any_of(fields.begin(), fields.end(),
                    [&](const RuntimeField& field)
                    { return field.Name == retainedField.first; });
                if (!known)
                    member.second.Members.push_back(retainedField);
            }
        }

        for (const Json5Value::Member& retainedMember : retained.Members)
        {
            if (fresh.Find(retainedMember.first) != nullptr)
                continue;
            if (retainedMember.first == "persistent_id")
                continue; // superseded by record-level identity
            if (EditorSceneSerializers().FindByJsonKey(retainedMember.first) != nullptr)
                continue; // known component, removed from the entity
            fresh.Members.push_back(retainedMember);
        }
    }
} // namespace

EditorDocument::EditorDocument(LoggingProvider& logging)
    : Registry_()
    , Scene(Registry_)
    , Logging(logging)
{
    Registry_.Id = { 2, 1 };
    Registry_.Kind = RegistryKind::Transient;
    Registry_.Zone = ZoneId{};

    Registry_.Resources.Register<ActiveCameraService>();

    // The document resolves authored identity the same way the runtime does, so
    // a stable id can be turned back into the entity holding it. The component's
    // own traits populate this; without the resource they are inert and every
    // lookup by persistent id is impossible.
    Registry_.Components.AddResource<PersistentEntityIndex>();

    // Component registration must happen before any entity is created.
    World& world = Registry_.Components;
    // Where a thing is, what it hangs off, and what it is called across a save,
    // through the same registrar the runtime composes from. Several of these
    // are pure-runtime columns the document never serializes -- parentage
    // persists as part of the scene's hierarchy, the world transform is
    // recomputed from it, the pose history is presentation -- but a document
    // that knows a smaller vocabulary than the runtime builds a smaller entity
    // from the same file, which is the discrepancy this avoids: a component's
    // owed set is provisioned only for what the world actually registered.
    ComponentRegistrar registrar(world);
    RegisterWorldComponents(registrar);
    world.RegisterComponent<BrushComponent>();
    world.RegisterComponent<BakedBrushComponent>();
    world.RegisterComponent<CameraComponent>();
    world.RegisterComponent<EntityNameComponent>();

    // Register storage for every serializer the registry knows — engine, editor,
    // and any game module loaded at startup — so game components are available
    // (and inspectable/addable) before any entity exists. Idempotent.
    for (const auto& serializer : EditorSceneSerializers().Entries())
        serializer->RegisterStorage(Registry_);

    // Storage is only half of what a loaded component needs. Tags, attributes,
    // abilities, and locomotion modes are named in content and resolved against
    // the registries that define them, so a document without those cannot read
    // back what it wrote. This installs the engine's own vocabulary; a loaded
    // game module adds its names to the same registries.
    RegisterMovement(world);
    InstallEditorModuleVocabulary(world);
}

void EditorDocument::SetAssetEnvironment(RuntimeAssets& assets)
{
    Assets = &assets.Assets;
    Catalog = &assets.Registry;

    // The lifecycle hooks for StaticMeshComponent retain/release through this
    // resource; without it an authored mesh handle would not hold its asset.
    World& world = Registry_.Components;
    if (!world.HasResource<StaticMeshComponentAssets>())
        world.AddResource<StaticMeshComponentAssets>(assets.StaticMeshes.get(), &assets.MaterialSets);
    if (!world.HasResource<SkinnedMeshComponentAssets>())
        world.AddResource<SkinnedMeshComponentAssets>(assets.SkinnedMeshes.get(), &assets.MaterialSets);
    if (!world.HasResource<AnimationClipComponentAssets>())
        world.AddResource<AnimationClipComponentAssets>(&assets.AnimationClips);
    // Registered empty with the movement components; this is the host naming
    // where its structured data lives. Without it an authored movement profile
    // is freed the moment the load lets go of it, and the document then saves a
    // handle that names nothing.
    if (auto* movement = world.TryGetResource<MovementComponentAssets>())
        movement->Profiles = &assets.DataAssets;
}

void EditorDocument::SetRegistryIdentity(RegistryId id, ZoneId zone)
{
    assert(Registry_.Components.EntityCount() == 0
           && "SetRegistryIdentity: document must be empty");
    Registry_.Id = id;
    Registry_.Zone = zone;
}

std::string_view EditorDocument::GetDisplayName() const
{
    return FilePath.empty() ? std::string_view("Untitled") : std::string_view(FilePath);
}

bool EditorDocument::IsDirty() const
{
    return Dirty;
}

Json5Value EditorDocument::SerializeEntityComponents(EntityId entity) const
{
    SceneSerializationContext context(Logging, Assets);
    Json5Value components = Json5Value::MakeObject();
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        if (serializer->JsonKey() == "persistent_id")
            continue;
        if (!serializer->HasComponent(entity, Registry_))
            continue;
        JsonWriteArchive archive;
        if (!serializer->Save(archive, entity, Registry_, context) || !archive.Ok())
            continue;
        JsonValue value = archive.TakeValue();
        if (!value.IsNull())
            components.Members.emplace_back(std::string(serializer->JsonKey()),
                                            Json5FromJson(value));
    }
    return components;
}

SceneSourceDocument EditorDocument::BuildSceneSource() const
{
    SceneSourceDocument out;
    out.Instances = Retained_.Instances;
    out.UnknownRoot = Retained_.UnknownRoot;
    out.LeadingComments = Retained_.LeadingComments;
    out.TrailingComments = Retained_.TrailingComments;
    out.EndComments = Retained_.EndComments;

    // Settings: retained members carried, the ones this build owns refreshed.
    out.Settings = Retained_.Settings.IsObject() ? Retained_.Settings
                                                 : Json5Value::MakeObject();
    std::erase_if(out.Settings.Members,
        [](const Json5Value::Member& member)
        { return member.first == "default_material"; });
    if (DefaultMaterial.IsValid())
        out.Settings.Members.emplace_back("default_material",
                                          Json5Value(DefaultMaterial.Path));


    std::unordered_map<std::uint64_t, const SceneSourceEntity*> retainedById;
    for (const SceneSourceEntity& entity : Retained_.Entities)
        retainedById.emplace(entity.Id.Value, &entity);

    SceneSerializationContext context(Logging, Assets);
    const World& world = Registry_.Components;
    // Only meshes the written entities reference: projection copies and other
    // strays must not accrete in the sidecar save over save.
    std::vector<BrushId> savedMeshes;
    for (EntityId entity : Scene.GetAllEntities())
    {
        const auto* id = world.TryGet<PersistentIdComponent>(entity);
        if (id == nullptr || !id->Id.IsValid())
            continue; // untracked interlopers have no place in the file
        // Derived projection entities live in their instance records, and
        // entities the harvest absorbed into add_entities live there too.
        if (Projection_.contains(id->Id.Value) || AbsorbedPids_.contains(id->Id.Value))
            continue;

        SceneSourceEntity record;
        record.Id = id->Id;
        if (const EntityId parent = Scene.GetParent(entity); parent.IsValid())
            if (const auto* parentId = world.TryGet<PersistentIdComponent>(parent))
                record.Parent = parentId->Id;
        record.Hidden = !Scene.IsEntityVisible(entity);
        record.Locked = Scene.IsEntityLocked(entity);
        if (const BrushComponent* brush = Scene.TryGetBrush(entity))
            savedMeshes.push_back(brush->Id);
        else if (const BakedBrushComponent* baked = Scene.TryGetBakedBrush(entity))
            savedMeshes.push_back(baked->Source);

        Json5Value fresh = SerializeEntityComponents(entity);

        if (const auto retained = retainedById.find(id->Id.Value);
            retained != retainedById.end())
        {
            MergeRetainedComponents(fresh, retained->second->Components);
            record.LeadingComments = retained->second->LeadingComments;
        }
        record.Components = std::move(fresh);
        out.Entities.push_back(std::move(record));
    }
    if (!savedMeshes.empty())
    {
        // Two entities may share one mesh (RestoreEntity keeps a restored
        // brush on its shared id); the file gets each mesh once.
        std::sort(savedMeshes.begin(), savedMeshes.end(),
                  [](BrushId a, BrushId b) { return a.Value < b.Value; });
        savedMeshes.erase(std::unique(savedMeshes.begin(), savedMeshes.end(),
                                      [](BrushId a, BrushId b)
                                      { return a.Value == b.Value; }),
                          savedMeshes.end());
        Json5Value meshes = Json5Value::MakeObject();
        for (const BrushId id : savedMeshes)
            if (const BrushMesh* mesh = Scene.GetBrushMeshStore().Find(id))
                meshes.Members.emplace_back(
                    std::to_string(id.Value),
                    Json5FromJson(BrushMeshToJson(*mesh)));
        out.BrushMeshes = std::move(meshes);
    }
    return out;
}

std::string EditorDocument::ToSceneText()
{
    HarvestInstanceOverrides();
    return WriteSceneSource(BuildSceneSource());
}

bool EditorDocument::LoadFromSceneText(std::string_view text, std::string* error)
{
    // Destructive on entry and on failure: a rejected file leaves this document
    // empty, not as it was. Callers that must survive a bad file load into a
    // fresh document and swap only on success (WorldDocument::Load, LoadZone).
    Scene.Clear();
    Retained_ = SceneSourceDocument{};

    std::string parseError;
    std::optional<SceneSourceDocument> parsed = ParseSceneSource(text, &parseError);
    if (!parsed.has_value())
    {
        Logging.GetLogger<EditorDocument>().Error("scene source: {}", parseError);
        if (error != nullptr)
            *error = std::move(parseError);
        Scene.SyncFromRegistry();
        return false;
    }

    const auto fail = [&](std::string message)
    {
        Logging.GetLogger<EditorDocument>().Error("scene source: {}", message);
        if (error != nullptr)
            *error = std::move(message);
        Scene.Clear();
        Scene.SyncFromRegistry();
        return false;
    };

    SceneSerializationContext context(Logging, Assets);
    for (const SceneSourceEntity& record : parsed->Entities)
    {
        const EntityId entity = Registry_.Components.CreateEntity();
        // Identity first: the component trait registers it in the index the
        // parent pass below resolves against.
        Registry_.Components.AddComponent(entity, PersistentIdComponent{ record.Id });

        for (const Json5Value::Member& member : record.Components.Members)
        {
            if (member.first == "persistent_id")
                continue; // the record id is authoritative
            IComponentSerializer* serializer =
                EditorSceneSerializers().FindByJsonKey(member.first);
            if (serializer == nullptr)
                continue; // unknown component: retained, not lost
            serializer->RegisterStorage(Registry_);
            const JsonValue component = Json5ToJson(member.second);
            JsonReadArchive archive(component);
            if (!serializer->Load(archive, entity, Registry_, context))
                return fail("entity " + PersistentEntityIdToString(record.Id)
                    + ": component '" + member.first + "' failed to load");
        }

        Scene.TrackEntity(entity);
        Scene.SetEntityVisible(entity, !record.Hidden);
        Scene.SetEntityLocked(entity, record.Locked);
    }

    // Parents second, once every record's identity is resolvable. A parent id
    // naming an instance stays unwired here: the instance's entities are a
    // derived projection, not part of the source load.
    if (const auto* index = Registry_.Components.TryGetResource<PersistentEntityIndex>())
        for (const SceneSourceEntity& record : parsed->Entities)
        {
            if (!record.Parent.IsValid())
                continue;
            const EntityId child = index->TryResolve(record.Id);
            const EntityId parent = index->TryResolve(record.Parent);
            if (child.IsValid() && parent.IsValid())
                (void)Scene.SetParent(child, parent);
        }

    if (parsed->BrushMeshes.IsObject() && !parsed->BrushMeshes.Members.empty())
        DeserializeBrushMeshes(Json5ToJson(parsed->BrushMeshes),
                               Scene.GetBrushMeshStore());

    if (const Json5Value* material = parsed->Settings.Find("default_material");
        material != nullptr && material->IsString())
    {
        DefaultMaterial = AssetRef{ AssetType::Material, material->Text };
    }

    if (Catalog != nullptr)
    {
        std::vector<std::string> refs;
        for (const SceneSourceEntity& record : parsed->Entities)
            CollectAssetRefs5(record.Components, refs);
        std::size_t missing = 0;
        Logger& log = Logging.GetLogger<EditorDocument>();
        for (const std::string& ref : refs)
            if (!Catalog->Contains(ref))
            {
                log.Warn("asset reference '{}' is unresolved (load)", ref);
                ++missing;
            }
        if (missing > 0)
            log.Warn("load: {} unresolved asset reference(s)", missing);
    }

    // Identity is authored, so a file that does not already carry it is rejected
    // rather than repaired (the parse enforces this; the check is the backstop).
    if (std::string identityError; !Scene.ValidateIdentities(&identityError))
        return fail("scene identity is invalid: " + identityError);

    Retained_ = std::move(*parsed);

    // Expand the instance records into derived entities. Not an authored
    // mutation: whatever it reports lands in the diagnostics, never in Dirty.
    Projection_.clear();
    RebuildSceneProjection();

    // The document was replaced wholesale, so whatever divergence the previous
    // contents had from disk went with them. Written directly because OnEdited
    // is for authored mutations, never loads.
    Dirty = false;
    return true;
}

EntitySnapshot EditorDocument::CaptureEntity(EntityId entity) const
{
    EntitySnapshot snapshot;
    SceneSerializationContext context(Logging, Assets);

    // One object per present component, keyed by JsonKey(): the same per-entity
    // layout SaveSceneJson produces, so RestoreEntity round-trips it.
    JsonValue::Object components;
    for (const auto& serializer : EditorSceneSerializers().Entries())
    {
        if (!serializer->HasComponent(entity, Registry_))
            continue;

        JsonWriteArchive archive;
        if (!serializer->Save(archive, entity, Registry_, context) || !archive.Ok())
            continue;

        JsonValue component = archive.TakeValue();
        if (!component.IsNull())
            components.emplace_back(std::string(serializer->JsonKey()), std::move(component));
    }
    snapshot.Components = JsonValue(std::move(components));

    // The brush mesh lives in the sidecar store, not the registry, so capture it
    // separately along with the id the brush component serialized. A baked brush
    // keeps its dormant source mesh under BakedBrushComponent instead.
    if (const BrushComponent* brush = Scene.TryGetBrush(entity))
    {
        snapshot.MeshId = brush->Id;
        if (const BrushMesh* mesh = Scene.TryGetBrushMesh(entity))
            snapshot.Mesh = *mesh;
    }
    else if (const BakedBrushComponent* baked = Scene.TryGetBakedBrush(entity))
    {
        snapshot.MeshId = baked->Source;
        if (const BrushMesh* mesh = Scene.TryGetDormantBrushMesh(entity))
            snapshot.Mesh = *mesh;
    }

    // Parent by persistent identity: the handle dies with the entities, the
    // identity survives the round trip through destruction and restore.
    if (const EntityId parent = Scene.GetParent(entity); parent.IsValid())
        if (const auto* parentId = Registry_.Components.TryGet<PersistentIdComponent>(parent))
            snapshot.ParentId = parentId->Id;

    snapshot.Hidden = !Scene.IsEntityVisible(entity);
    snapshot.Locked = Scene.IsEntityLocked(entity);
    return snapshot;
}

EntityId EditorDocument::RestoreEntity(const EntitySnapshot& snapshot, bool freshMesh)
{
    SceneSerializationContext context(Logging, Assets);
    EntityId entity = Registry_.Components.CreateEntity();

    if (snapshot.Components.IsObject())
    {
        for (const auto& [key, componentData] : snapshot.Components.AsObject())
        {
            IComponentSerializer* serializer = nullptr;
            for (const auto& entry : EditorSceneSerializers().Entries())
                if (entry->JsonKey() == key)
                {
                    serializer = entry.get();
                    break;
                }
            if (serializer == nullptr)
                continue;

            serializer->RegisterStorage(Registry_);
            JsonReadArchive archive(componentData);
            serializer->Load(archive, entity, Registry_, context);
        }
    }

    // Adoption settles identity, and it follows liveness: an undone delete or a
    // cross-zone move restores its snapshot id (nothing live holds it), a
    // duplicate or copy of a live source mints fresh, and a recipe snapshot with
    // no id at all gets one. The components are loaded first so the id the
    // snapshot carries is the one being judged.
    Scene.TrackEntity(entity);

    if (snapshot.Mesh.has_value())
    {
        if (freshMesh)
        {
            // Source is still alive and owns snapshot.MeshId: give the copy its own
            // mesh and repoint whichever component carries it (brush, or the baked
            // dormant source), so the two entities are independent.
            const BrushId id = Scene.GetBrushMeshStore().Create(*snapshot.Mesh);
            if (Scene.TryGetBrush(entity) != nullptr)
                Scene.SetComponent(entity, BrushComponent{ id });
            else if (Scene.TryGetBakedBrush(entity) != nullptr)
                Scene.SetComponent(entity, BakedBrushComponent{ id });
        }
        else if (Scene.GetBrushMeshStore().Find(snapshot.MeshId) == nullptr)
        {
            // Re-seat at the original id (the delete-undo case; the id is free
            // because destroy released it). BrushMeshStore::NextId is monotonic
            // and never reuses a freed id, so this cannot collide with a Create.
            // When the id is still LIVE the restore is an instance of an alive
            // mesh: leave the store alone so the shared mesh keeps any edits
            // made since the snapshot.
            Scene.GetBrushMeshStore().Set(snapshot.MeshId, *snapshot.Mesh);
        }
    }

    // The parent resolves through live identity, so restore order matters to a
    // subtree: a composite that destroyed leaf-up restores parent-first, and
    // each child finds its parent already re-registered here. A parent that no
    // longer exists leaves the entity unparented rather than failing the
    // restore.
    if (snapshot.ParentId.IsValid())
        if (const auto* index = Registry_.Components.TryGetResource<PersistentEntityIndex>())
        {
            const EntityId parent = index->TryResolve(snapshot.ParentId);
            if (parent.IsValid())
                (void)Scene.SetParent(entity, parent);
        }

    Scene.SetEntityVisible(entity, !snapshot.Hidden);
    Scene.SetEntityLocked(entity, snapshot.Locked);
    return entity;
}

EntityId EditorDocument::DuplicateEntity(EntityId source)
{
    return RestoreEntity(CaptureEntity(source), /*freshMesh*/ true);
}

JsonValue EditorDocument::CaptureComponent(EntityId entity,
                                          const IComponentSerializer& serializer) const
{
    SceneSerializationContext context(Logging, Assets);
    JsonWriteArchive archive;
    if (!serializer.Save(archive, entity, Registry_, context) || !archive.Ok())
        return {};
    return archive.TakeValue();
}

bool EditorDocument::RestoreComponent(EntityId entity,
                                     IComponentSerializer& serializer,
                                     const JsonValue& snapshot)
{
    SceneSerializationContext context(Logging, Assets);
    serializer.RegisterStorage(Registry_);
    JsonReadArchive archive(snapshot);
    return serializer.Load(archive, entity, Registry_, context);
}

bool EditorDocument::Save()
{
    if (FilePath.empty())
        return false;

    HarvestInstanceOverrides();
    const SceneSourceDocument source = BuildSceneSource();
    if (Catalog != nullptr)
    {
        std::vector<std::string> refs;
        for (const SceneSourceEntity& record : source.Entities)
            CollectAssetRefs5(record.Components, refs);
        std::size_t missing = 0;
        Logger& log = Logging.GetLogger<EditorDocument>();
        for (const std::string& ref : refs)
            if (!Catalog->Contains(ref))
            {
                log.Warn("asset reference '{}' is unresolved (save)", ref);
                ++missing;
            }
        if (missing > 0)
            log.Warn("save: {} unresolved asset reference(s)", missing);
    }
    const std::string text = WriteSceneSource(source);

    std::ofstream file(FilePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    file << text;
    if (!file.good())
        return false;

    Dirty = false;
    return true;
}

bool EditorDocument::SaveAs(std::string_view path)
{
    if (path.empty())
        return false;

    FilePath.assign(path);
    return Save();
}

bool EditorDocument::Load(std::string_view path)
{
    std::ifstream file{ std::string(path), std::ios::binary };
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (!LoadFromSceneText(buffer.str()))
        return false;

    FilePath.assign(path);
    return true;
}

void EditorDocument::New()
{
    Scene.Clear();
    Retained_ = SceneSourceDocument{};
    Projection_.clear();
    AbsorbedPids_.clear();
    ProjectionDiagnostics_ = ProjectionDiagnostics{};
    FilePath.clear();
    Dirty = false;
}

bool EditorDocument::HasFilePath() const
{
    return !FilePath.empty();
}

void EditorDocument::MarkDirty(bool dirty)
{
    Dirty = dirty;
    if (dirty && OnEdited)
        OnEdited();
}

EditorScene& EditorDocument::GetScene()
{
    return Scene;
}

const EditorScene& EditorDocument::GetScene() const
{
    return Scene;
}

const Registry& EditorDocument::GetRegistry() const
{
    return Registry_;
}

const AssetRef& EditorDocument::GetDefaultMaterial() const
{
    return DefaultMaterial;
}

void EditorDocument::SetDefaultMaterial(AssetRef material)
{
    DefaultMaterial = std::move(material);
}
