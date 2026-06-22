#include "LevelDocument.h"

#include "brush/BrushMeshSerialization.h"

#include <core/assets/AssetRegistry.h>
#include <core/assets/RuntimeAssets.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <core/serialization/JsonArchive.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <render/Camera.h>
#include <render/StaticMeshComponent.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializationContext.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    // Collects every asset:// string anywhere in the JSON, shape-agnostic: a bare
    // path value or a path nested in an {id, path} ref both surface, and any asset
    // kind (mesh, material, future types) is found with no per-type knowledge.
    void CollectAssetRefs(const JsonValue& value, std::vector<std::string>& out)
    {
        if (value.IsString())
        {
            const std::string& text = value.AsString();
            if (text.rfind("asset://", 0) == 0)
                out.push_back(text);
        }
        else if (value.IsObject())
        {
            for (const auto& member : value.AsObject())
                CollectAssetRefs(member.second, out);
        }
        else if (value.IsArray())
        {
            for (const JsonValue& element : value.AsArray())
                CollectAssetRefs(element, out);
        }
    }

    // Warns about references the catalog can't resolve. Fires when a level is
    // reopened after a referenced asset was removed (the codec also fails the
    // field; this is the consolidated, user-facing summary). A guard, not a gate:
    // it never blocks load or save.
    void ReportUnresolvedAssetRefs(const JsonValue& scene, const AssetRegistry& catalog,
                                   Logger& log, std::string_view when)
    {
        std::vector<std::string> refs;
        CollectAssetRefs(scene, refs);

        std::size_t missing = 0;
        for (const std::string& ref : refs)
            if (!catalog.Contains(ref))
            {
                log.Warn("asset reference '{}' is unresolved ({})", ref, when);
                ++missing;
            }
        if (missing > 0)
            log.Warn("{}: {} unresolved asset reference(s)", when, missing);
    }
} // namespace

LevelDocument::LevelDocument(LoggingProvider& logging)
    : Registry_()
    , Scene(Registry_)
    , Logging(logging)
{
    Registry_.Id = { 2, 1 };
    Registry_.Kind = RegistryKind::Transient;
    Registry_.Zone = ZoneId::Invalid();

    Registry_.Resources.Register<ActiveCameraService>();

    // Component registration must happen before any entity is created.
    World& world = Registry_.Components;
    world.RegisterComponent<LocalTransform>();
    world.RegisterComponent<BrushComponent>();
    world.RegisterComponent<CameraComponent>();

    // Register storage for every serializer the registry knows — engine, editor,
    // and any game module loaded at startup — so game components are available
    // (and inspectable/addable) before any entity exists. Idempotent.
    for (const auto& serializer : GetComponentSerializerEntries())
        serializer->RegisterStorage(Registry_);
}

void LevelDocument::SetAssetEnvironment(RuntimeAssets& assets)
{
    Assets = &assets.Assets;
    Catalog = &assets.Registry;

    // The lifecycle hooks for StaticMeshComponent retain/release through this
    // resource; without it an authored mesh handle would not hold its asset.
    World& world = Registry_.Components;
    if (!world.HasResource<StaticMeshComponentAssets>())
        world.AddResource<StaticMeshComponentAssets>(&assets.StaticMeshes, &assets.MaterialSets);
}

std::string_view LevelDocument::GetDisplayName() const
{
    return FilePath.empty() ? std::string_view("Untitled") : std::string_view(FilePath);
}

bool LevelDocument::IsDirty() const
{
    return Dirty;
}

JsonValue LevelDocument::ToJson() const
{
    // Assets may be null (brush-only); the codec only touches it on asset fields,
    // of which a brush-only scene has none.
    SceneSerializationContext context(Logging, Assets);
    JsonValue root = SaveSceneJson(Registry_, context);
    if (root.IsObject())
    {
        root.AsObject().emplace_back("brush_meshes", SerializeBrushMeshes(Scene.GetBrushMeshStore()));
        if (DefaultMaterial.IsValid())
            root.AsObject().emplace_back("default_material", JsonValue(DefaultMaterial.Path));
    }
    return root;
}

bool LevelDocument::LoadFromJson(const JsonValue& root)
{
    Scene.Clear();

    SceneLoadError loadError;
    SceneSerializationContext context(Logging, Assets);
    if (!LoadSceneJson(root, Registry_, context, &loadError))
    {
        Scene.SyncFromRegistry();
        return false;
    }

    if (const JsonValue* meshes = root.Find("brush_meshes"))
        DeserializeBrushMeshes(*meshes, Scene.GetBrushMeshStore());

    if (const JsonValue* material = root.Find("default_material"); material && material->IsString())
        DefaultMaterial = AssetRef{ AssetType::Material, material->AsString() };

    if (Catalog != nullptr)
        ReportUnresolvedAssetRefs(root, *Catalog, Logging.GetLogger<LevelDocument>(), "load");

    Scene.SyncFromRegistry();
    return true;
}

EntitySnapshot LevelDocument::CaptureEntity(EntityId entity) const
{
    EntitySnapshot snapshot;
    SceneSerializationContext context(Logging, Assets);

    // One object per present component, keyed by JsonKey(): the same per-entity
    // layout SaveSceneJson produces, so RestoreEntity round-trips it.
    JsonValue::Object components;
    for (const auto& serializer : GetComponentSerializerEntries())
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
    // separately along with the id the brush component serialized.
    if (const BrushComponent* brush = Scene.TryGetBrush(entity))
    {
        snapshot.MeshId = brush->Id;
        if (const BrushMesh* mesh = Scene.TryGetBrushMesh(entity))
            snapshot.Mesh = *mesh;
    }

    snapshot.Hidden = !Scene.IsEntityVisible(entity);
    snapshot.Locked = Scene.IsEntityLocked(entity);
    return snapshot;
}

EntityId LevelDocument::RestoreEntity(const EntitySnapshot& snapshot, bool freshMesh)
{
    SceneSerializationContext context(Logging, Assets);
    EntityId entity = Registry_.Components.CreateEntity();

    if (snapshot.Components.IsObject())
    {
        for (const auto& [key, componentData] : snapshot.Components.AsObject())
        {
            IComponentSerializer* serializer = nullptr;
            for (const auto& entry : GetComponentSerializerEntries())
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

    Scene.TrackEntity(entity);

    if (snapshot.Mesh.has_value())
    {
        if (freshMesh)
        {
            // Source is still alive and owns snapshot.MeshId: give the copy its own
            // mesh and repoint its brush component, so the two are independent.
            const BrushId id = Scene.GetBrushMeshStore().Create(*snapshot.Mesh);
            Scene.SetComponent(entity, BrushComponent{ id });
        }
        else
        {
            // Re-seat at the original id. BrushMeshStore::NextId is monotonic and
            // never reuses a freed id, so this cannot collide with a Create.
            Scene.GetBrushMeshStore().Set(snapshot.MeshId, *snapshot.Mesh);
        }
    }

    Scene.SetEntityVisible(entity, !snapshot.Hidden);
    Scene.SetEntityLocked(entity, snapshot.Locked);
    return entity;
}

EntityId LevelDocument::DuplicateEntity(EntityId source)
{
    return RestoreEntity(CaptureEntity(source), /*freshMesh*/ true);
}

JsonValue LevelDocument::CaptureComponent(EntityId entity,
                                          const IComponentSerializer& serializer) const
{
    SceneSerializationContext context(Logging, Assets);
    JsonWriteArchive archive;
    if (!serializer.Save(archive, entity, Registry_, context) || !archive.Ok())
        return {};
    return archive.TakeValue();
}

bool LevelDocument::RestoreComponent(EntityId entity,
                                     IComponentSerializer& serializer,
                                     const JsonValue& snapshot)
{
    SceneSerializationContext context(Logging, Assets);
    serializer.RegisterStorage(Registry_);
    JsonReadArchive archive(snapshot);
    return serializer.Load(archive, entity, Registry_, context);
}

bool LevelDocument::Save()
{
    if (FilePath.empty())
        return false;

    const JsonValue json = ToJson();
    if (Catalog != nullptr)
        ReportUnresolvedAssetRefs(json, *Catalog, Logging.GetLogger<LevelDocument>(), "save");
    const std::string text = JsonStringify(json, /*pretty*/ true);

    std::ofstream file(FilePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;

    file << text;
    if (!file.good())
        return false;

    Dirty = false;
    return true;
}

bool LevelDocument::SaveAs(std::string_view path)
{
    if (path.empty())
        return false;

    FilePath.assign(path);
    return Save();
}

bool LevelDocument::Load(std::string_view path)
{
    std::ifstream file{ std::string(path), std::ios::binary };
    if (!file.is_open())
        return false;

    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsonParseError parseError;
    const std::optional<JsonValue> root = JsonParse(buffer.str(), &parseError);
    if (!root.has_value())
        return false;

    if (!LoadFromJson(*root))
        return false;

    FilePath.assign(path);
    Dirty = false;
    return true;
}

void LevelDocument::New()
{
    Scene.Clear();
    FilePath.clear();
    Dirty = false;
}

bool LevelDocument::HasFilePath() const
{
    return !FilePath.empty();
}

void LevelDocument::MarkDirty(bool dirty)
{
    Dirty = dirty;
}

LevelScene& LevelDocument::GetScene()
{
    return Scene;
}

const LevelScene& LevelDocument::GetScene() const
{
    return Scene;
}

const Registry& LevelDocument::GetRegistry() const
{
    return Registry_;
}

const AssetRef& LevelDocument::GetDefaultMaterial() const
{
    return DefaultMaterial;
}

void LevelDocument::SetDefaultMaterial(AssetRef material)
{
    DefaultMaterial = std::move(material);
}
