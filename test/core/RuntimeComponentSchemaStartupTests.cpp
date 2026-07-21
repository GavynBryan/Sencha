#include <app/Application.h>
#include <app/Engine.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeSet.h>
#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <camera/CameraRegistration.h>
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
#include <movement/MovementRegistration.h>
#include <movement/MovementState.h>
#include <physics/PhysicsRegistration.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <render/PointLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <world/RuntimeComponentSchema.h>
#include <world/registry/Registry.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>
#include <zone/DefaultZoneBuilder.h>

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

template <>
struct TypeSchema<StartupGameComponent>
{
    static constexpr std::string_view Name = "startup_game_component";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('S', 'G', 'A', 'M');

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
    static constexpr std::string_view Name = "missing_runtime_component";
    static constexpr std::uint32_t SceneChunkId = MakeFourCC('M', 'I', 'S', 'S');

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
void ExpectSameComponentId(const World& current, const World& unified)
{
    ASSERT_TRUE(current.IsRegistered<T>());
    ASSERT_TRUE(unified.IsRegistered<T>());
    EXPECT_EQ(current.GetComponentId<T>(), unified.GetComponentId<T>());
}

class RuntimeSchemaGame final : public Game
{
public:
    void OnConfigure(GameConfigureContext& ctx) override
    {
        ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
        ctx.Config.Debug.ConsoleLogging = false;
    }

    void OnRegisterComponents(ComponentSerializerRegistry&) override
    {
        InitSceneSerializer();
        RegisterComponent<StartupGameComponent>();
    }

    void OnUnregisterComponents(ComponentSerializerRegistry& serializers) override
    {
        serializers.Remove(ResolveComponentTypeId<StartupGameComponent>());
    }

    void OnRegisterRuntimeComponents(WorldComponentSchema& schema) override
    {
        ++RegistrationCalls;
        EXPECT_FALSE(schema.IsSealed());
        EXPECT_TRUE(schema.Contains(ResolveComponentTypeId<LocalTransform>()));
        EXPECT_TRUE(schema.Add<StartupGameComponent>());
    }

    void OnStart(GameStartupContext&) override
    {
        ++StartCalls;
        const WorldComponentSchema& schema = GetEngine().RuntimeComponents();
        SawSealedSchema = schema.IsSealed();
        SawEngineComponent =
            schema.Contains(ResolveComponentTypeId<RigidBody>());
        SawGameComponent =
            schema.Contains(ResolveComponentTypeId<StartupGameComponent>());
        SchemaSize = schema.Size();

        World world;
        schema.Apply(world);
        AppliedGameComponent = world.IsRegistered<StartupGameComponent>();
        AppliedGameComponentId = world.GetComponentId<StartupGameComponent>();
    }

    int RegistrationCalls = 0;
    int StartCalls = 0;
    bool SawSealedSchema = false;
    bool SawEngineComponent = false;
    bool SawGameComponent = false;
    bool AppliedGameComponent = false;
    std::size_t SchemaSize = 0;
    ComponentId AppliedGameComponentId = InvalidComponentId;
};

class MissingRuntimeSchemaGame final : public Game
{
public:
    void OnConfigure(GameConfigureContext& ctx) override
    {
        ctx.Config.Window.GraphicsApi = WindowGraphicsApi::None;
        ctx.Config.Debug.ConsoleLogging = false;
    }

    void OnRegisterComponents(ComponentSerializerRegistry&) override
    {
        InitSceneSerializer();
        RegisterComponent<MissingRuntimeComponent>();
    }

    void OnUnregisterComponents(ComponentSerializerRegistry& serializers) override
    {
        ++UnregisterCalls;
        serializers.Remove(ResolveComponentTypeId<MissingRuntimeComponent>());
    }

    void OnStart(GameStartupContext&) override
    {
        ++StartCalls;
    }

    int StartCalls = 0;
    int UnregisterCalls = 0;
};
} // namespace

TEST(RuntimeComponentSchema, EnginePrefixMatchesCurrentZoneRegistrationOrder)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();

    EXPECT_EQ(schema.Size(), 24u);

    World unified;
    schema.Apply(unified);

    Registry current = MakeZoneRegistry(RegistryId{ 2, 1 }, ZoneId{ 1 });
    InitializeDefault3DRegistry(
        current,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr);
    RegisterPhysicsComponents(current.Components);
    RegisterMovementComponents(current.Components);
    RegisterCameraComponents(current.Components);

    ExpectSameComponentId<LocalTransform>(current.Components, unified);
    ExpectSameComponentId<WorldTransform>(current.Components, unified);
    ExpectSameComponentId<Parent>(current.Components, unified);
    ExpectSameComponentId<CameraComponent>(current.Components, unified);
    ExpectSameComponentId<StaticMeshComponent>(current.Components, unified);
    ExpectSameComponentId<PointLightComponent>(current.Components, unified);
    ExpectSameComponentId<AudioSourceComponent>(current.Components, unified);
    ExpectSameComponentId<AudioCaptionComponent>(current.Components, unified);
    ExpectSameComponentId<Collider>(current.Components, unified);
    ExpectSameComponentId<RigidBody>(current.Components, unified);
    ExpectSameComponentId<CharacterController>(current.Components, unified);
    ExpectSameComponentId<PhysicsBodyLink>(current.Components, unified);
    ExpectSameComponentId<CharacterMoverLink>(current.Components, unified);
    ExpectSameComponentId<GameplayTagContainer>(current.Components, unified);
    ExpectSameComponentId<AttributeSet>(current.Components, unified);
    ExpectSameComponentId<AbilitySet>(current.Components, unified);
    ExpectSameComponentId<ActiveEffect>(current.Components, unified);
    ExpectSameComponentId<MovementIntent>(current.Components, unified);
    ExpectSameComponentId<MovementState>(current.Components, unified);
    ExpectSameComponentId<MovementProfile>(current.Components, unified);
    ExpectSameComponentId<OnGround>(current.Components, unified);
    ExpectSameComponentId<InAir>(current.Components, unified);
    ExpectSameComponentId<LocomotionModeRequest>(current.Components, unified);
    ExpectSameComponentId<CameraRig>(current.Components, unified);
}

TEST(RuntimeComponentSchema, EngineRunSealsEngineAndGameVocabularyBeforeStart)
{
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");

    Application app(0, nullptr);
    RuntimeSchemaGame game;

    ASSERT_EQ(app.Run(game), 0);

    EXPECT_EQ(game.RegistrationCalls, 1);
    EXPECT_EQ(game.StartCalls, 1);
    EXPECT_TRUE(game.SawSealedSchema);
    EXPECT_TRUE(game.SawEngineComponent);
    EXPECT_TRUE(game.SawGameComponent);
    EXPECT_TRUE(game.AppliedGameComponent);
    EXPECT_EQ(game.SchemaSize, 25u);
    EXPECT_EQ(game.AppliedGameComponentId, 24u);
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
