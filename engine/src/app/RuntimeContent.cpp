#include <app/RuntimeContent.h>

#include <anim/AnimRigBinding.h>
#include <app/DefaultRenderPipeline.h>
#include <app/Engine.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#ifdef SENCHA_ENABLE_COOK
#include <assets/hotreload/SourceReloadRoots.h>
#endif
#include <assets/runtime/ContentTagDeclarations.h>
#include <audio/AudioSourceRuntime.h>
#include <core/assets/AssetStoreTable.h>
#include <core/config/EngineConfig.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <graphics/vulkan/GraphicsServices.h>
#include <input/InputBindingCache.h>
#include <movement/MovementProfileBindingCache.h>
#include <runtime/spawn/NetPrefabSpawner.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>

#include <cassert>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace
{
#ifdef SENCHA_ENABLE_COOK
// Polls the watched sources; a changed one stages an in-place swap that
// commits at the async drain. The poll throttles itself.
struct HotReloadPollSystem
{
    explicit HotReloadPollSystem(RuntimeContent& content)
        : Content(content)
    {
    }

    void FrameUpdate(FrameUpdateContext&)
    {
        if (SourceReloadRoots* sources = Content.SourceReload())
            (void)sources->Poll(std::chrono::steady_clock::now());
    }

    RuntimeContent& Content;
};
#endif
} // namespace

RuntimeContent::RuntimeContent(Engine& engine, Logger& log)
    : Host(engine)
    , Log(log)
{
    LoggingProvider& logging = engine.Logging();

    // A dedicated host has no graphics services, so it composes an asset stack
    // that cannot hold a mesh or a texture and loads everything else -- the
    // movement profiles it simulates from, the collision it collides with --
    // through the same front door.
    if (GraphicsServices* graphics = engine.TryGraphics(); graphics != nullptr)
    {
        Assets_.emplace(
            logging,
            graphics->Buffers,
            graphics->Images,
            graphics->Descriptors,
            graphics->Samplers,
            engine.SceneSerializers());
    }
    else
    {
        Assets_.emplace(logging, engine.SceneSerializers());
    }

    SceneContextState = std::make_unique<SceneSerializationContext>(
        logging, &Assets_->Assets);
}

RuntimeContent::~RuntimeContent()
{
    // Destroying a published stack leaves the world naming caches that no
    // longer exist, and the failure surfaces later as a call through a
    // destroyed vtable. Disconnect is what makes that impossible, so a path
    // that skips it is a bug in the caller's teardown, not something to
    // silently repair here.
    assert(!Published
           && "RuntimeContent destroyed while still published; Disconnect first");
}

void RuntimeContent::Mount()
{
    for (const std::string& root : Host.Config().Runtime.ContentRoots)
    {
        const ContentRootPaths paths = ResolveContentRoot(root);
        MountContentRoot(paths, *Assets_, Log);
        MountedRoots.push_back(paths);
    }

#ifdef SENCHA_ENABLE_COOK
    SourceReloadState = std::make_unique<SourceReloadRoots>(
        Host.Logging(), &Host.Jobs(), Host.Tasks(), Assets_->Assets, Assets_->Registry);
    for (const ContentRootPaths& root : MountedRoots)
        SourceReloadState->AddRoot(root.Authored.string(), { ".sdata" });
#endif
}

void RuntimeContent::Publish(World& world)
{
    RuntimeAssets& assets = *Assets_;

    world.SetResource(assets.Assets.Stores());
    world.SetResource(AudioSourceRuntime{
        &assets.AudioClips, &Host.Audio(), &Host.Captions() });
    world.SetResource(AnimRigBindings{ &assets.DataAssets, &assets.AnimationClips, &assets.Skeletons });

    // The names content declares, before anything binds against them.
    if (GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
    {
        std::vector<std::string> errors;
        DeclareContentTags(assets, *tags, errors);
        for (const std::string& error : errors)
            Log.Error("gameplay tags: {}", error);
    }

    // The spawn services are engine-owned; the asset stack they resolve scenes
    // through is this one. The second is for the spawns a peer names rather
    // than this machine asking for: without it every replicated prefab is
    // unbuildable and every body a client is sent is deferred forever.
    Host.Spawns().ConnectAssets(&assets.Assets, &assets.Scenes);
    Host.NetPrefabs().ConnectAssets(&assets.Assets, &assets.Scenes);

    // The pipeline object exists headless -- it is registered unconditionally
    // and its extract hook is simply never dispatched -- so the guard that
    // matters is the graphics services its mesh feature is built from.
    GraphicsServices* graphics = Host.TryGraphics();
    if (DefaultRenderPipeline* pipeline = Host.GetRenderPipeline();
        pipeline != nullptr && graphics != nullptr)
    {
        pipeline->SetAssetStores(
            *assets.StaticMeshes,
            assets.Materials,
            assets.MaterialSets,
            assets.Textures.get(),
            assets.SkinnedMeshes.get(),
            &assets.Skeletons);
        pipeline->AddMeshRenderFeature(*graphics);
    }

    Published = true;
}

void RuntimeContent::RegisterSystems(EngineSchedule& schedule)
{
#ifdef SENCHA_ENABLE_COOK
    schedule.Register<HotReloadPollSystem>(*this);
#else
    (void)schedule;
#endif
}

void RuntimeContent::Disconnect(World& world)
{
    if (!Published)
        return;
    Published = false;

    // The world-resource binding caches hold leases into the data-asset cache;
    // every reference must drop before the stack goes away. A lease that
    // outlives its owner calls through a destroyed vtable when the world tears
    // down, which aborts on the way out rather than at the point of the mistake.
    if (MovementProfileBindingCache* bindings =
            world.TryGetResource<MovementProfileBindingCache>())
    {
        bindings->Clear();
    }
    if (InputBindingCache* bindings = world.TryGetResource<InputBindingCache>())
    {
        bindings->Clear();
    }

    // The prefab spawner holds a scene reference per resident prefab for the
    // length of the run. Disconnecting drops those while the caches that issued
    // them are still here.
    Host.Spawns().ConnectAssets(nullptr, nullptr);
    Host.NetPrefabs().ConnectAssets(nullptr, nullptr);

    world.SetResource(AssetStoreTable{});
    world.SetResource(AudioSourceRuntime{});
    world.SetResource(AnimRigBindings{});

#ifdef SENCHA_ENABLE_COOK
    SourceReloadState.reset();
#endif
}

RuntimeAssets& RuntimeContent::Assets()
{
    return *Assets_;
}

const RuntimeAssets& RuntimeContent::Assets() const
{
    return *Assets_;
}

SceneSerializationContext& RuntimeContent::SceneContext()
{
    return *SceneContextState;
}

std::span<const ContentRootPaths> RuntimeContent::Roots() const
{
    return MountedRoots;
}
