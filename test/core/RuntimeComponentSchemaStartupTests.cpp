#include <app/Application.h>
#include <app/Engine.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeSet.h>
#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <camera/CameraRig.h>
#include <components/CameraComponent.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/ActiveEffect.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementIntent.h>
#include <movement/MovementModes.h>
#include <movement/MovementProfile.h>
#include <movement/MovementState.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <render/IrradianceVolumeComponent.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/ComponentManifest.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldConnectionComponents.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

struct StartupGameComponent
{
    int Value = 0;
};

struct MissingRuntimeComponent
{
    int Value = 0;
};

SENCHA_DECLARE_COMPONENT_TYPE(
    StartupGameComponent,
    "test.startup_game_component");
SENCHA_DECLARE_COMPONENT_TYPE(
    MissingRuntimeComponent,
    "test.missing_runtime_component");

namespace
{
int StartupGameRemoveCalls = 0;
}

template <>
struct ComponentTraits<StartupGameComponent>
{
    static void OnRemove(
        const StartupGameComponent&,
        World&,
        EntityId)
    {
        ++StartupGameRemoveCalls;
    }
};

template <>
struct TypeSchema<StartupGameComponent>
{
    static constexpr std::string_view Name =
        "startup_game_component";
    static constexpr std::uint32_t SceneChunkId =
        MakeFourCC('S', 'G', 'A', 'M');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &StartupGameComponent::Value),
        };
    }
};

template <>
struct TypeSchema<MissingRuntimeComponent>
{
    static constexpr std::string_view Name =
        "missing_runtime_component";
    static constexpr std::uint32_t SceneChunkId =
        MakeFourCC('M', 'I', 'S', 'S');

    static auto Fields()
    {
        return std::tuple{
            MakeField("value", &MissingRuntimeComponent::Value),
        };
    }
};

namespace
{
template <typename T>
void ExpectComponentId(const World& world, ComponentId expected)
{
    ASSERT_TRUE(world.IsRegistered<T>());
    EXPECT_EQ(world.GetComponentId<T>(), expected);
}

class RuntimeSchemaGame final : public Game
{
public:
    void OnConfigure(GameConfigureContext& ctx) override
    {
        ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
        ctx.Config.Debug.ConsoleLogging = false;
    }

    void OnRegisterComponents(
        ComponentSerializerRegistry& serializers) override
    {
        RegisterComponent<StartupGameComponent>(serializers);
    }

    void OnUnregisterComponents(
        ComponentSerializerRegistry& serializers) override
    {
        serializers.Remove(
            ResolveComponentTypeId<StartupGameComponent>());
    }

    void OnRegisterRuntimeComponents(
        WorldComponentSchema& schema) override
    {
        ++RegistrationCalls;
        EXPECT_FALSE(schema.IsSealed());
        EXPECT_TRUE(schema.Contains(
            ResolveComponentTypeId<LocalTransform>()));
        EXPECT_TRUE(schema.Add<StartupGameComponent>());
    }

    void OnStart(GameStartupContext&) override
    {
        ++StartCalls;
        const WorldComponentSchema& schema =
            GetEngine().RuntimeComponents();
        SawSealedSchema = schema.IsSealed();
        SawEngineComponent = schema.Contains(
            ResolveComponentTypeId<RigidBody>());
        SawGameComponent = schema.Contains(
            ResolveComponentTypeId<StartupGameComponent>());
        SchemaSize = schema.Size();

        World world;
        schema.Apply(world);
        AppliedGameComponent =
            world.IsRegistered<StartupGameComponent>();
        AppliedGameComponentId =
            world.GetComponentId<StartupGameComponent>();

        RuntimeWorld& runtime = GetEngine().World();
        World& live = runtime.Entities();
        SawEngineOwnedWorld =
            live.IsRegistered<StartupGameComponent>();
        EngineOwnedGameComponentId =
            live.GetComponentId<StartupGameComponent>();

        const EntityId entity = live.CreateEntity();
        live.AddComponent<StartupGameComponent>(
            entity,
            StartupGameComponent{ 42 });
        EngineOwnedEntityWasPersistent =
            live.GetEntityPartition(entity)
            == PersistentStoragePartition;
    }

    int RegistrationCalls = 0;
    int StartCalls = 0;
    bool SawSealedSchema = false;
    bool SawEngineComponent = false;
    bool SawGameComponent = false;
    bool AppliedGameComponent = false;
    bool SawEngineOwnedWorld = false;
    bool EngineOwnedEntityWasPersistent = false;
    std::size_t SchemaSize = 0;
    ComponentId AppliedGameComponentId = InvalidComponentId;
    ComponentId EngineOwnedGameComponentId = InvalidComponentId;
};

class MissingRuntimeSchemaGame final : public Game
{
public:
    void OnConfigure(GameConfigureContext& ctx) override
    {
        ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
        ctx.Config.Debug.ConsoleLogging = false;
    }

    void OnRegisterComponents(
        ComponentSerializerRegistry& serializers) override
    {
        RegisterComponent<MissingRuntimeComponent>(serializers);
    }

    void OnUnregisterComponents(
        ComponentSerializerRegistry& serializers) override
    {
        ++UnregisterCalls;
        serializers.Remove(
            ResolveComponentTypeId<MissingRuntimeComponent>());
    }

    void OnStart(GameStartupContext&) override
    {
        ++StartCalls;
    }

    int StartCalls = 0;
    int UnregisterCalls = 0;
};
} // namespace

TEST(RuntimeComponentSchema, EngineSchemaCoversEverySceneComponent)
{
    // A component reaches the serializer through the manifest. Without a column
    // to deserialize into, that only fails once a scene naming it is loaded, so
    // the coverage is asserted here rather than left to a startup check.
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();

    ForEachSceneComponent([&]<typename T>(ComponentTag<T>)
    {
        EXPECT_TRUE(schema.Contains(ResolveComponentTypeId<T>()))
            << "manifest component " << TypeSchema<T>::Name
            << " has no runtime storage";
    });
}

TEST(RuntimeComponentSchema, EngineSchemaUsesCanonicalComponentIds)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();

    EXPECT_EQ(schema.Size(), 30u);

    World world;
    schema.Apply(world);

    ExpectComponentId<LocalTransform>(world, 0);
    ExpectComponentId<WorldTransform>(world, 1);
    ExpectComponentId<Parent>(world, 2);
    ExpectComponentId<CameraComponent>(world, 3);
    ExpectComponentId<StaticMeshComponent>(world, 4);
    ExpectComponentId<ZoneLightmapComponent>(world, 5);
    ExpectComponentId<IrradianceVolumeComponent>(world, 6);
    ExpectComponentId<PointLightComponent>(world, 7);
    ExpectComponentId<SpotLightComponent>(world, 8);
    ExpectComponentId<AudioSourceComponent>(world, 9);
    ExpectComponentId<AudioCaptionComponent>(world, 10);
    ExpectComponentId<WorldDock>(world, 11);
    ExpectComponentId<WorldLink>(world, 12);
    ExpectComponentId<DockGateBinding>(world, 13);
    ExpectComponentId<Collider>(world, 14);
    ExpectComponentId<RigidBody>(world, 15);
    ExpectComponentId<CharacterController>(world, 16);
    ExpectComponentId<PhysicsBodyLink>(world, 17);
    ExpectComponentId<CharacterMoverLink>(world, 18);
    ExpectComponentId<GameplayTagContainer>(world, 19);
    ExpectComponentId<AttributeSet>(world, 20);
    ExpectComponentId<AbilitySet>(world, 21);
    ExpectComponentId<ActiveEffect>(world, 22);
    ExpectComponentId<MovementIntent>(world, 23);
    ExpectComponentId<MovementState>(world, 24);
    ExpectComponentId<MovementProfile>(world, 25);
    ExpectComponentId<OnGround>(world, 26);
    ExpectComponentId<InAir>(world, 27);
    ExpectComponentId<LocomotionModeRequest>(world, 28);
    ExpectComponentId<CameraRig>(world, 29);
}

TEST(RuntimeComponentSchema, EngineOwnsUnifiedWorldBeforeGameStart)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    StartupGameRemoveCalls = 0;

    Application app(0, nullptr);
    RuntimeSchemaGame game;

    ASSERT_EQ(app.Run(game), 0);

    EXPECT_EQ(game.RegistrationCalls, 1);
    EXPECT_EQ(game.StartCalls, 1);
    EXPECT_TRUE(game.SawSealedSchema);
    EXPECT_TRUE(game.SawEngineComponent);
    EXPECT_TRUE(game.SawGameComponent);
    EXPECT_TRUE(game.AppliedGameComponent);
    EXPECT_TRUE(game.SawEngineOwnedWorld);
    EXPECT_TRUE(game.EngineOwnedEntityWasPersistent);
    EXPECT_EQ(game.SchemaSize, 31u);
    EXPECT_EQ(game.AppliedGameComponentId, 30u);
    EXPECT_EQ(game.EngineOwnedGameComponentId, 30u);
    EXPECT_EQ(StartupGameRemoveCalls, 1);
}

TEST(RuntimeComponentSchema, MissingRuntimeStorageFailsBeforeGameStart)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    Application app(0, nullptr);
    MissingRuntimeSchemaGame game;

    EXPECT_EQ(app.Run(game), 1);
    EXPECT_EQ(game.StartCalls, 0);
    EXPECT_EQ(game.UnregisterCalls, 1);
}
