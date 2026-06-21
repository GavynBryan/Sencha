#pragma once

#include "EntitySnapshot.h"
#include "LevelScene.h"

#include <core/assets/AssetRef.h>
#include <core/json/JsonValue.h>
#include <world/registry/Registry.h>

#include <string>
#include <string_view>

class LoggingProvider;
class AssetSystem;
class AssetRegistry;
struct RuntimeAssets;
struct IComponentSerializer;

class LevelDocument
{
public:
    // The logger is always present (a sink-less LoggingProvider is a silent
    // no-op, which is how headless cooks and tests run), so the document always
    // serializes through a SceneSerializationContext: one path, no "is logging
    // wired yet" branch. The asset system is the separately-optional half.
    explicit LevelDocument(LoggingProvider& logging);

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

    [[nodiscard]] std::string_view GetDisplayName() const;
    [[nodiscard]] bool IsDirty() const;
    bool Save();
    bool SaveAs(std::string_view path);
    bool Load(std::string_view path);
    void New();

    // In-memory serialization (scene + brush meshes + default material). Save and
    // Load are the file-backed wrappers; the live-document cook snapshots the
    // editor's current state through these without writing the .level file, so an
    // unsaved level still cooks.
    [[nodiscard]] JsonValue ToJson() const;
    bool LoadFromJson(const JsonValue& root);

    // Captures an entity's full persistent state (every registered component via
    // the serializer registry, plus the brush sidecar mesh and view flags) so a
    // deletion can be undone. RestoreEntity recreates it and returns the new id
    // (a fresh generational handle: the original index/generation is not reused).
    [[nodiscard]] EntitySnapshot CaptureEntity(EntityId entity) const;
    EntityId RestoreEntity(const EntitySnapshot& snapshot);

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
    [[nodiscard]] LevelScene& GetScene();
    [[nodiscard]] const LevelScene& GetScene() const;
    [[nodiscard]] const Registry& GetRegistry() const;

    // Level-wide fallback material applied to any face that carries no explicit
    // one (a fresh brush is never "no material"). A level setting. (04-§2)
    [[nodiscard]] const AssetRef& GetDefaultMaterial() const;
    void SetDefaultMaterial(AssetRef material);

private:
    std::string FilePath;
    bool Dirty = false;
    Registry Registry_;
    LevelScene Scene;
    AssetRef DefaultMaterial{ AssetType::Material, "asset://materials/dev/gray.smat" };

    // Always present (constructor-injected). The asset system and catalog are
    // non-owning and optional: null until SetAssetEnvironment (brush-only).
    LoggingProvider& Logging;
    AssetSystem* Assets = nullptr;
    AssetRegistry* Catalog = nullptr;
};
