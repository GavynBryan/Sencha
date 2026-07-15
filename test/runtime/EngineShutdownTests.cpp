#include <gtest/gtest.h>

#include <app/Engine.h>
#include <ecs/ComponentTraits.h>
#include <ecs/World.h>
#include <world/registry/Registry.h>

#include <SDL3/SDL_hints.h>

struct EngineShutdownProbe
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(EngineShutdownProbe, "test.engine_shutdown_probe");

struct EngineShutdownState
{
    Engine* Owner = nullptr;
    int* RemoveCount = nullptr;
    bool* SawInitializedEngine = nullptr;
    bool* AudioWasAccessible = nullptr;
};

template <>
struct ComponentTraits<EngineShutdownProbe>
{
    static void OnRemove(const EngineShutdownProbe&,
                         World& world,
                         EntityId)
    {
        EngineShutdownState& state = world.GetResource<EngineShutdownState>();
        if (state.RemoveCount != nullptr)
            ++*state.RemoveCount;

        const bool initialized = state.Owner != nullptr && state.Owner->IsInitialized();
        if (state.SawInitializedEngine != nullptr)
            *state.SawInitializedEngine = initialized;

        if (initialized)
        {
            (void)state.Owner->Audio();
            if (state.AudioWasAccessible != nullptr)
                *state.AudioWasAccessible = true;
        }
    }
};

TEST(EngineShutdown, ClearsRegistryEntitiesBeforeServices)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    EngineConfig config;
    config.Window.GraphicsApi = WindowGraphicsApi::None;
    config.Debug.ConsoleLogging = false;
    config.Runtime.JobWorkerCount = 0;

    Engine engine(config);
    ASSERT_TRUE(engine.Initialize());

    int removeCount = 0;
    bool sawInitializedEngine = false;
    bool audioWasAccessible = false;

    World& world = engine.Zones().Global().Components;
    world.RegisterComponent<EngineShutdownProbe>();
    world.AddResource<EngineShutdownState>(EngineShutdownState{
        .Owner = &engine,
        .RemoveCount = &removeCount,
        .SawInitializedEngine = &sawInitializedEngine,
        .AudioWasAccessible = &audioWasAccessible,
    });

    const EntityId entity = world.CreateEntity();
    world.AddComponent(entity, EngineShutdownProbe{ 1 });

    engine.Shutdown();

    EXPECT_EQ(removeCount, 1);
    EXPECT_TRUE(sawInitializedEngine);
    EXPECT_TRUE(audioWasAccessible);
    EXPECT_EQ(world.EntityCount(), 0u);
    EXPECT_FALSE(engine.IsInitialized());
}
