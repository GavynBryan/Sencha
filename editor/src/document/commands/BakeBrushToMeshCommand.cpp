#include "BakeBrushToMeshCommand.h"

#include "../BrushBake.h"
#include "../EditorDocument.h"
#include "../EditorScene.h"

#include <assets/static_mesh/MeshSerializer.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <core/hash/ContentHash.h>
#include <core/logging/Logger.h>
#include <render/StaticMeshComponent.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{
// Resolve the baked asset + its per-slot materials and attach the component.
// Load/Acquire each add a caller reference; the component's OnAdd hook retains
// its own, so the caller references are released before returning: the
// component alone owns the assets afterwards, exactly like a scene-loaded one.
bool AttachStaticMeshComponent(EditorScene& scene, AssetSystem& assets, EntityId entity,
                               const std::string& meshPath, const std::vector<AssetRef>& materials)
{
    const StaticMeshHandle mesh = assets.LoadStaticMesh(meshPath);
    if (!mesh.IsValid())
        return false;

    std::vector<MaterialHandle> slots;
    slots.reserve(materials.size());
    for (const AssetRef& material : materials)
        slots.push_back(material.Path.empty() ? MaterialHandle{} : assets.LoadMaterial(material.Path));
    const MaterialSetHandle set = assets.AcquireMaterialSet(slots);
    for (MaterialHandle slot : slots)
        if (slot.IsValid())
            assets.ReleaseMaterial(slot); // the set holds its own member references

    scene.GetRegistry().Components.AddComponent(
        entity, StaticMeshComponent{ .Mesh = mesh, .Materials = set });

    assets.ReleaseStaticMesh(mesh);
    assets.ReleaseMaterialSet(set);
    return true;
}

class BakeBrushToMeshCommand : public ICommand
{
public:
    BakeBrushToMeshCommand(EditorScene& scene, EditorDocument& document, AssetSystem& assets,
                           EntityId entity, BrushId brushId, std::string meshPath,
                           std::vector<AssetRef> materials)
        : Scene(scene), Document(document), Assets(assets), Entity(entity), Brush(brushId)
        , MeshPath(std::move(meshPath)), Materials(std::move(materials))
    {
    }

    void Execute() override
    {
        World& world = Scene.GetRegistry().Components;
        if (!AttachStaticMeshComponent(Scene, Assets, Entity, MeshPath, Materials))
            return;
        world.RemoveComponent<BrushComponent>(Entity);
        world.AddComponent(Entity, BakedBrushComponent{ Brush });
        Document.MarkDirty();
    }

    void Undo() override
    {
        World& world = Scene.GetRegistry().Components;
        world.RemoveComponent<StaticMeshComponent>(Entity);
        world.RemoveComponent<BakedBrushComponent>(Entity);
        world.AddComponent(Entity, BrushComponent{ Brush });
        Document.MarkDirty();
    }

private:
    EditorScene& Scene;
    EditorDocument& Document;
    AssetSystem& Assets;
    EntityId Entity;
    BrushId Brush; // stays alive in the BrushMeshStore across the swap
    std::string MeshPath;
    std::vector<AssetRef> Materials;
};

class RevertBakedBrushCommand : public ICommand
{
public:
    RevertBakedBrushCommand(EditorScene& scene, EditorDocument& document, AssetSystem& assets,
                            EntityId entity, BrushId brushId, std::string meshPath,
                            std::vector<AssetRef> materials)
        : Scene(scene), Document(document), Assets(assets), Entity(entity), Brush(brushId)
        , MeshPath(std::move(meshPath)), Materials(std::move(materials))
    {
    }

    void Execute() override
    {
        World& world = Scene.GetRegistry().Components;
        world.RemoveComponent<StaticMeshComponent>(Entity);
        world.RemoveComponent<BakedBrushComponent>(Entity);
        world.AddComponent(Entity, BrushComponent{ Brush });
        Document.MarkDirty();
    }

    void Undo() override
    {
        World& world = Scene.GetRegistry().Components;
        if (!AttachStaticMeshComponent(Scene, Assets, Entity, MeshPath, Materials))
            return;
        world.RemoveComponent<BrushComponent>(Entity);
        world.AddComponent(Entity, BakedBrushComponent{ Brush });
        Document.MarkDirty();
    }

private:
    EditorScene& Scene;
    EditorDocument& Document;
    AssetSystem& Assets;
    EntityId Entity;
    BrushId Brush;
    std::string MeshPath;
    std::vector<AssetRef> Materials;
};
}

std::unique_ptr<ICommand> MakeBakeBrushToMeshCommand(EditorScene& scene,
                                                     EditorDocument& document,
                                                     AssetSystem& assets,
                                                     AssetRegistry& catalog,
                                                     LoggingProvider& logging,
                                                     EntityId entity,
                                                     const std::filesystem::path& contentRoot)
{
    Logger& log = logging.GetLogger<BakeBrushToMeshCommand>();

    const BrushComponent* brush = scene.TryGetBrush(entity);
    const BrushMesh* mesh = scene.TryGetBrushMesh(entity);
    if (brush == nullptr || mesh == nullptr)
        return nullptr;

    MeshGeometry geometry;
    std::vector<AssetRef> materials;
    std::string error;
    if (!BakeBrushToGeometry(*mesh, document.GetDefaultMaterial(), geometry, materials, &error))
    {
        log.Warn("bake: {}", error);
        return nullptr;
    }

    // Content-hashed name: re-baking identical geometry reuses the same file,
    // and distinct bakes cannot collide.
    const uint64_t hash = HashBytes64(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(geometry.Vertices.data()),
        geometry.Vertices.size() * sizeof(StaticMeshVertex)));
    char name[64];
    std::snprintf(name, sizeof(name), "brush_%u_%08x.smesh",
                  static_cast<unsigned>(brush->Id.Value),
                  static_cast<unsigned>(hash & 0xFFFFFFFFu));

    const std::string relative = std::string("meshes/baked/") + name;
    const std::filesystem::path physical = contentRoot / relative;
    const std::string assetPath = std::string("asset://") + relative;

    std::error_code ec;
    std::filesystem::create_directories(physical.parent_path(), ec);
    MeshSerializer serializer(logging);
    if (!serializer.WriteToFile(physical.string(), geometry))
    {
        log.Warn("bake: failed to write '{}'", physical.string());
        return nullptr;
    }

    // Register immediately so this session resolves the path; the next session
    // picks the file up from the content scan.
    if (catalog.FindByPath(assetPath) == nullptr)
    {
        AssetRecord record;
        record.Type = AssetType::StaticMesh;
        record.SourceKind = AssetSourceKind::File;
        record.Path = assetPath;
        record.FilePath = physical.string();
        record.ContentHash = hash;
        (void)catalog.Register(record);
    }

    log.Info("baked brush entity to '{}' ({} section(s))", assetPath, geometry.Sections.size());
    return std::make_unique<BakeBrushToMeshCommand>(scene, document, assets, entity, brush->Id,
                                                    assetPath, std::move(materials));
}

std::unique_ptr<ICommand> MakeRevertBakedBrushCommand(EditorScene& scene,
                                                      EditorDocument& document,
                                                      AssetSystem& assets,
                                                      EntityId entity)
{
    const BakedBrushComponent* baked = scene.TryGetBakedBrush(entity);
    if (baked == nullptr || scene.TryGetDormantBrushMesh(entity) == nullptr)
        return nullptr;

    // Capture the component's asset paths so redo can re-attach them.
    std::string meshPath;
    std::vector<AssetRef> materials;
    const World& world = std::as_const(scene).GetRegistry().Components;
    if (const StaticMeshComponent* component = world.TryGet<StaticMeshComponent>(entity))
    {
        meshPath = assets.GetPathForStaticMesh(component->Mesh);
        if (const std::vector<MaterialHandle>* slots = assets.GetMaterialSet(component->Materials))
            for (MaterialHandle slot : *slots)
                materials.push_back(AssetRef{ AssetType::Material,
                                              std::string(assets.GetPathForMaterial(slot)) });
    }

    return std::make_unique<RevertBakedBrushCommand>(scene, document, assets, entity,
                                                     baked->Source, std::move(meshPath),
                                                     std::move(materials));
}
