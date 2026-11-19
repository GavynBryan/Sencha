#include <save/EngineSaveSections.h>

#include <app/Engine.h>
#include <app/RuntimeContent.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RuntimeAssets.h>
#include <assets/scene/SceneCache.h>
#include <core/assets/AssetLease.h>
#include <core/serialization/Serialize.h>
#include <ecs/World.h>
#include <math/MathSchemas.h>
#include <participant/ParticipantControl.h>
#include <participant/ParticipantLifecycle.h>
#include <participant_spawn/RestoredBodies.h>
#include <save/HeldRecordSaves.h>
#include <save/SavedGames.h>
#include <world/RuntimeWorld.h>
#include <world/build/EntityBuildPackage.h>
#include <world/identity/PersistentIdMint.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SmapFormat.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace
{
    constexpr std::uint32_t kBodiesVersion = 1;
    constexpr std::uint32_t kHeldVersion = 1;

    EntityId LocalBodyOf(const World& world)
    {
        const EntityId participant = LocalParticipantOf(world);
        const ParticipantControl* control =
            participant.IsValid() ? world.TryGet<ParticipantControl>(participant) : nullptr;
        return control != nullptr && world.IsAlive(control->Body) ? control->Body : EntityId{};
    }

    std::optional<EntityBuildPackage> PrefabOf(Engine& engine, const World& world, EntityId body)
    {
        const SceneInstance* instance = world.IsRegistered<SceneInstance>() ? world.TryGet<SceneInstance>(body) : nullptr;
        RuntimeAssets& assets = engine.Content().Assets();
        const AssetRecord* record = instance != nullptr ? assets.Registry.FindById(instance->Source) : nullptr;
        if (record == nullptr)
            return std::nullopt;
        const AssetLease lease = assets.Assets.LoadLease(record->Path, AssetType::Scene);
        const SmapContents* contents =
            lease.IsValid() ? assets.Scenes.Get(SceneHandle::FromToken(lease.OpaqueToken())) : nullptr;
        EntityBuildPackage prefab;
        if (contents == nullptr
            || !BuildEntityPackageFromSmap(*contents, engine.SceneSerializers(), prefab,
                                           SmapPackageOptions{ .StripAuthoredIdentity = true }))
            return std::nullopt;
        return prefab;
    }

    JsonValue CaptureBodies(Engine& engine, World& world)
    {
        const EntityId body = LocalBodyOf(world);
        if (!body.IsValid())
            return JsonValue(JsonValue::Object{});
        (void)EnsurePersistentId(world, body);
        const std::optional<EntityBuildPackage> prefab = PrefabOf(engine, world, body);
        if (!prefab.has_value())
            return JsonValue(JsonValue::Object{});
        const SceneSpawnOverlay overlay =
            CaptureSpawnOverlay(world, body, *prefab, engine.SceneSerializers(), engine.Content().SceneContext());
        const LocalTransform* placed = world.TryGet<LocalTransform>(body);
        JsonValue::Object local{ { "root", ToJson(placed != nullptr ? placed->Value : Transform3f::Identity()) },
                                 { "patch", overlay.RootPatch } };
        if (overlay.RootId.IsValid())
            local.emplace_back("id", JsonValue(PersistentEntityIdToString(overlay.RootId)));
        return JsonValue(JsonValue::Object{ { "local", JsonValue(std::move(local)) } });
    }

    void RestoreBodies(World& world, std::uint32_t, const JsonValue& data)
    {
        RestoredBodies* existing = world.TryGetResource<RestoredBodies>();
        const JsonValue* local = data.Find("local");
        if (local == nullptr || !local->IsObject())
        {
            if (existing != nullptr)
                existing->Local.reset();
            return;
        }
        RestoredBody restored;
        if (const JsonValue* root = local->Find("root"))
            (void)FromJson(*root, restored.Root);
        if (const JsonValue* patch = local->Find("patch"); patch != nullptr && patch->IsObject())
            restored.Overlay.RootPatch = *patch;
        if (const JsonValue* id = local->Find("id"); id != nullptr && id->IsString())
            restored.Overlay.RootId = PersistentEntityIdFromString(id->AsString()).value_or(PersistentEntityId{});
        if (existing != nullptr)
            existing->Local = std::move(restored);
        else
            world.AddResource<RestoredBodies>().Local = std::move(restored);
    }

}

void RegisterEngineSaveSections(SavedGames& saves, Engine& engine)
{
    (void)saves.RegisterSection(SavedGameSectionDefinition{
        .Name = "sencha.bodies",
        .Version = kBodiesVersion,
        .Capture = [&engine](World& world) { return CaptureBodies(engine, world); },
        .Restore = &RestoreBodies });
    (void)saves.RegisterSection(SavedGameSectionDefinition{
        .Name = "sencha.held",
        .Version = kHeldVersion,
        .Capture =
            [&engine](World& world) {
                const EntityId body = LocalBodyOf(world);
                return CaptureHeldRecords(world, std::span(&body, body.IsValid() ? 1 : 0), engine.SceneSerializers(),
                                          engine.Content().SceneContext());
            },
        .Restore =
            [&engine](World& world, std::uint32_t, const JsonValue& data) {
                RestoreHeldRecords(world, data, engine.SceneSerializers(), engine.Content().SceneContext());
            } });
}
