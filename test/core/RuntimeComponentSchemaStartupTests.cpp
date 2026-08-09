#include <app/Application.h>
#include <app/Engine.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeSet.h>
#include <audio/AudioCaptionComponent.h>
#include <audio/AudioSourceComponent.h>
#include <camera/CameraRig.h>
#include <components/CameraComponent.h>
#include <core/console/ConsoleService.h>
#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <core/serialization/FourCC.h>
#include <ecs/WorldComponentSchema.h>
#include <effects/ActiveEffect.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <controller/LookOrientation.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementComponents.h>
#include <movement/JumpState.h>
#include <movement/MovementIntent.h>
#include <net/NetReplicationComponents.h>
#include <input/InputActionSource.h>
#include <net/NetSpawnRecipe.h>
#include <physics/components/CharacterController.h>
#include <physics/components/CharacterMoverLink.h>
#include <physics/components/Collider.h>
#include <physics/components/PhysicsBodyLink.h>
#include <physics/components/RigidBody.h>
#include <render/IrradianceVolumeComponent.h>
#include <world/transform/TransformHistory.h>
#include <render/PointLightComponent.h>
#include <render/SpotLightComponent.h>
#include <render/StaticMeshComponent.h>
#include <render/ZoneLightmapComponent.h>
#include <world/ComponentRegistrar.h>
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

    void OnRegisterComponents(ComponentRegistrar& registrar) override
    {
        ++RegistrationCalls;
        registrar.Add<StartupGameComponent>();
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

        // The table has no list to read it off, so the console command is how
        // anyone finds out what this build replicates. Exercised here because
        // it needs a composed engine to have anything to print.
        const ConsoleResult dump =
            GetEngine().Console().ExecuteLine("net_components");
        ComponentDumpOk = dump.Succeeded();
        for (const ConsoleOutputEntry& entry : dump.Output)
            ComponentDumpText += entry.Text;

        // Headless still runs the frame loop; this test only wants the hooks.
        GetEngine().RequestExit();
    }

    bool ComponentDumpOk = false;
    std::string ComponentDumpText;
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

} // namespace

// Storage, serializers, and the replicated table are filled from one set of
// declarations, so the thing worth asserting is that they still describe the
// same components -- not the contents of a list, which is what the manifest
// tuples used to make this test about.
TEST(RuntimeComponentSchema, OneRegistrationFillsEveryRegistryConsistently)
{
    WorldComponentSchema schema;
    ComponentSerializerRegistry serializers;
    ReplicationLayout layout;

    ComponentRegistrar registrar(&schema, &serializers, &layout);
    RegisterEngineComponents(registrar);
    schema.Seal();

    ASSERT_EQ(layout.Error(), ReplicationLayoutError::None)
        << ReplicationLayoutErrorToString(layout.Error()) << ": " << layout.ErrorDetail();

    std::string missing;
    EXPECT_TRUE(RuntimeComponentSchemaCoversSerializers(schema, serializers, &missing))
        << "serialized component without storage: " << missing;
    EXPECT_TRUE(RuntimeComponentSchemaCoversReplication(schema, layout, &missing))
        << "replicated component without storage: " << missing;

    // Both policies are read off the schema, so a component that declares one
    // is in that registry and a component that declares neither is in neither.
    EXPECT_NE(serializers.FindByType(ResolveComponentTypeId<PointLightComponent>()), nullptr);
    EXPECT_EQ(layout.Find(ResolveComponentTypeId<PointLightComponent>()), nullptr);

    EXPECT_NE(layout.Find(ResolveComponentTypeId<LookOrientation>()), nullptr);
    EXPECT_EQ(serializers.FindByType(ResolveComponentTypeId<LookOrientation>()), nullptr);

    // LocalTransform is the one that does both.
    EXPECT_NE(serializers.FindByType(ResolveComponentTypeId<LocalTransform>()), nullptr);
    EXPECT_NE(layout.Find(ResolveComponentTypeId<LocalTransform>()), nullptr);

    // And a runtime-only column is in storage alone.
    EXPECT_TRUE(schema.Contains(ResolveComponentTypeId<WorldTransform>()));
    EXPECT_EQ(serializers.FindByType(ResolveComponentTypeId<WorldTransform>()), nullptr);
    EXPECT_EQ(layout.Find(ResolveComponentTypeId<WorldTransform>()), nullptr);
}

// What the order actually has to guarantee. It used to be pinned as forty-six
// literal indices, which made every added component a twenty-line edit and
// asserted a great deal that nothing depends on: a component's saved identity
// is the chunk id its schema declares and its runtime identity is a hash of its
// name, so neither moves when this order does.
TEST(RuntimeComponentSchema, TheTransformTrioLeadsTheVocabulary)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();

    World world;
    schema.Apply(world);

    ExpectComponentId<LocalTransform>(world, 0);
    ExpectComponentId<WorldTransform>(world, 1);
    ExpectComponentId<Parent>(world, 2);
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
    EXPECT_EQ(StartupGameRemoveCalls, 1);

    EXPECT_TRUE(game.ComponentDumpOk);
    EXPECT_NE(game.ComponentDumpText.find("Transform"), std::string::npos);
    EXPECT_NE(game.ComponentDumpText.find("owner-only"), std::string::npos);

    // The engine's vocabulary comes first and the game's follows it, stated
    // against what the engine actually registers rather than against a number
    // that has to be edited every time a component is added.
    WorldComponentSchema engineOnly;
    RegisterEngineRuntimeComponents(engineOnly);
    EXPECT_EQ(game.SchemaSize, engineOnly.Size() + 1);
    EXPECT_EQ(game.AppliedGameComponentId, engineOnly.Size());
    EXPECT_EQ(game.EngineOwnedGameComponentId, engineOnly.Size());
}

// The startup guard against a serializer with no column to deserialize into.
// Registration no longer offers a way to produce one -- naming a component adds
// storage and its serializer together -- so this exercises the predicate
// directly rather than through a game that cannot construct the mismatch.
TEST(RuntimeComponentSchema, StorageCoverageNamesTheComponentItIsMissing)
{
    WorldComponentSchema schema;
    RegisterEngineRuntimeComponents(schema);
    schema.Seal();

    ComponentSerializerRegistry serializers;
    ComponentRegistrar engineSerializers(nullptr, &serializers, nullptr);
    RegisterEngineComponents(engineSerializers);
    EXPECT_TRUE(RuntimeComponentSchemaCoversSerializers(schema, serializers));

    // A serializer that reached the registry by some other route than the
    // registrar -- which is the only way this can now happen.
    RegisterComponent<MissingRuntimeComponent>(serializers);

    std::string missing;
    EXPECT_FALSE(RuntimeComponentSchemaCoversSerializers(schema, serializers, &missing));
    EXPECT_EQ(missing, "missing_runtime_component");
}
