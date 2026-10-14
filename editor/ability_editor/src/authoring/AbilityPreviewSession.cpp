#include "authoring/AbilityPreviewSession.h"

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityLocals.h>
#include <anim/AnimRequestSet.h>
#include <app/EngineSchedule.h>
#include <app/EngineVocabulary.h>
#include <app/EngineVocabularyBindings.h>
#include <app/GameContexts.h>
#include <assets/runtime/RuntimeAssets.h>
#include <assets/runtime/ContentVocabulary.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <controller/Aim.h>
#include <controller/LookOrientation.h>
#include <core/config/EngineConfig.h>
#include <ecs/World.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputActionVocabulary.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementRegistration.h>
#include <movement/components/CharacterMovement.h>
#include <physics/PhysicsQueries.h>
#include <physics/PhysicsRegistration.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/PhysicsWorld.h>
#include <physics/RigidBodyBinding.h>
#include <physics/components/CharacterController.h>
#include <runtime/RuntimeFrameLoop.h>
#include <time/ActorTimeLeases.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
// Holds the time-scale control's lease; any key the fixture's own abilities never mint.
constexpr std::uint64_t kControlLeaseKey = ~std::uint64_t{ 0 };
}

struct AbilityPreviewSession::Systems
{
    EngineConfig Config;
    RuntimeFrameLoop Runtime;
    EngineSchedule Kit;
};

AbilityPreviewSession::AbilityPreviewSession(RuntimeAssets& assets, std::function<void(World&)> vocabulary)
    : Assets(assets)
    , Vocabulary(std::move(vocabulary))
{
    AllPartitions.Add(StoragePartitionId::Default());
}

AbilityPreviewSession::~AbilityPreviewSession()
{
    DropWorld();
}

void AbilityPreviewSession::Show(std::string path, JsonValue data)
{
    Path = std::move(path);
    Data = std::move(data);
    if (Clock.IsBuilt())
        Clock.Replay();
    else
        Clock.Restart();
}

void AbilityPreviewSession::Close()
{
    DropWorld();
    Clock.Close();
    Actions.clear();
    Issues.clear();
    Path.clear();
    Data = {};
}

void AbilityPreviewSession::DropWorld()
{
    Bindings.reset();
    Schedule.reset();
    Queries.reset();
    Verbs.reset();
    Preview.reset();
    FixtureEntity = {};
    Shown = {};
}

const AbilityDefinition* AbilityPreviewSession::Definition() const
{
    return Preview != nullptr && Shown.IsValid() ? Preview->GetResource<AbilityRegistry>().Get(Shown) : nullptr;
}

const CharacterController* AbilityPreviewSession::FixtureCapsule() const
{
    return Preview != nullptr ? static_cast<const World&>(*Preview).TryGet<CharacterController>(FixtureEntity) : nullptr;
}

void AbilityPreviewSession::Control(AbilityPreviewAction action)
{
    if (Clock.Branch())
        std::erase_if(Actions, [now = Clock.Now()](const AbilityPreviewAction& kept) { return kept.Tick > now; });
    action.Tick = Clock.NextTick();
    Actions.push_back(std::move(action));
}

bool AbilityPreviewSession::Rebuild()
{
    DropWorld();
    Issues.clear();
    SimulationClock = FixedSimulationLoop(1.0 / kTickRate);
    Preview = std::make_unique<World>();
    World& world = *Preview;
    RegisterAbilityKit(world);
    {
        ComponentRegistrar registrar(world);
        RegisterEngineComponents(registrar);
    }
    RegisterMovement(world);
    InstallAuthoredVocabulary(world);
    (void)DeclareEngineVocabulary(world);
    if (Vocabulary)
        Vocabulary(world);
    DeclareContentVocabulary(Assets, world, ContentVocabularyReload::Once, Issues);
    Issues.clear();

    // The working data over whatever its saved file declared.
    AbilityDeclaration declaration;
    std::vector<AssetRef> dependencies;
    std::vector<AbilityDiagnostic> diagnostics;
    AbilityDefinition definition;
    if (Data.IsNull() || !CompileAbilityDeclaration(Data, declaration, dependencies, diagnostics)
        || !ResolveAbilityDeclaration(declaration, Path, MakeAbilityResolveEnvironment(world), definition, diagnostics))
    {
        for (const AbilityDiagnostic& diagnostic : diagnostics)
            if (diagnostic.Severity == AbilityDiagnosticSeverity::Error)
                Issues.push_back(std::format("{}: {}", diagnostic.Path, diagnostic.Message));
        if (Issues.empty())
            Issues.push_back("Nothing to preview: open an ability.");
        return false;
    }
    const std::optional<AbilityBindingDecl> binding = definition.InputDefault;
    Shown = world.GetResource<AbilityRegistry>().Define(Path, std::move(definition));

    ActionNames.clear();
    if (const AbilityBindingDecl effective = EffectiveAbilityBinding({}, binding); effective.Action)
        ActionNames.push_back(*effective.Action);
    std::vector<InputActionDefinition> actions;
    for (const std::string& name : ActionNames)
        actions.push_back({ .Name = name });
    (void)ActionSet.Rebuild(actions);
    world.SetResource(InputActionVocabulary{ &ActionSet, 1 });
    world.AddResource<InputActionState>().Configure(actions.size());
    Held.assign(actions.size(), false);

    Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(world));
    Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(world));
    // The systems a game registers for a character that acts and moves.
    Schedule = std::make_unique<Systems>();
    RegisterPhysics(Schedule->Kit);
    RegisterAbilityKitSystems(Schedule->Kit, AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() });
    RegisterMovementSystems(Schedule->Kit, Assets.DataAssets);
    Schedule->Kit.Init();
    PhysicsWorld& physics = Schedule->Kit.Get<PhysicsStepSystem>()->GetSimulation();
    Bindings = std::make_unique<EngineVocabularyBindings>(world, SimulationClock, *Verbs);
    Bindings->BindPhysics(physics, *Queries);

    BodyDesc floor;
    floor.Shape = CollisionShape::MakeBox(Vec3d(50.0f, 0.5f, 50.0f));
    floor.Position = Vec3d(0.0f, -0.5f, 0.0f);
    floor.Motion = BodyMotion::Static;
    floor.Layer = CollisionLayer::Static;
    (void)physics.AddBody(floor);

    const CharacterController capsule;
    FixtureEntity = world.CreateEntity();
    world.AddComponent<LocalTransform>(
        FixtureEntity,
        LocalTransform{ Transform3f{ Vec3d(0.0f, capsule.Height * 0.5f, 0.0f), Quatf::Identity(), Vec3d::One() } });
    world.AddComponent<CharacterController>(FixtureEntity, capsule);
    world.AddComponent<CharacterMovement>(
        FixtureEntity, CharacterMovement{ .Mode = world.GetResource<LocomotionModeRegistry>().FreeMode() });
    world.AddComponent<LookOrientation>(FixtureEntity, LookOrientation{});
    world.AddComponent<InputActionSourceRef>(FixtureEntity, InputActionSourceRef{});
    // A pawn takes animation requests whether or not a rig plays them.
    world.AddComponent<AnimRequestSet>(FixtureEntity, AnimRequestSet{});
    world.AddComponent<AttributeSet>(FixtureEntity);
    const AttributeRegistry& attributes = world.GetResource<AttributeRegistry>();
    AttributeSet& set = *world.TryGet<AttributeSet>(FixtureEntity);
    for (std::uint32_t id = 1; id <= attributes.Size(); ++id)
        (void)set.Add(AttributeId{ id }, attributes.DefaultBase(AttributeId{ id }));
    world.AddComponent<AbilityGrants>(FixtureEntity, AbilityGrants{});
    const std::uint16_t entry = world.GetResource<AbilityGrantEntries>().Intern({ "preview", Shown, {} });
    (void)GrantAbility(*world.TryGet<AbilityGrants>(FixtureEntity), Shown, AbilityGrantKind::Authored, entry);
    return true;
}

void AbilityPreviewSession::Apply(const AbilityPreviewAction& action, std::uint64_t tick,
                                  AbilityPreviewTickRecord& record)
{
    World& world = *Preview;
    const auto edge = [&](InputEdgeKind kind) {
        const InputActionId id = ActionSet.Find(action.Name);
        if (!id.IsValid())
            return;
        const auto index = static_cast<std::uint8_t>(InputActionRegistry::IndexOf(id));
        (void)world.GetResource<InputActionState>().TickEdgeStorage().Append({ index, kind });
        Held[index] = kind == InputEdgeKind::Press;
    };
    switch (action.Control)
    {
    case AbilityPreviewControl::Press: edge(InputEdgeKind::Press); break;
    case AbilityPreviewControl::Release: edge(InputEdgeKind::Release); break;
    case AbilityPreviewControl::Interrupt: edge(InputEdgeKind::InterruptedRelease); break;
    case AbilityPreviewControl::Tap:
        for (std::uint32_t i = 0; i < action.Count; ++i)
        {
            edge(InputEdgeKind::Press);
            edge(InputEdgeKind::Release);
        }
        break;
    case AbilityPreviewControl::SetAttribute:
        if (const AttributeId id = world.GetResource<AttributeRegistry>().FindAttribute(action.Name); id.IsValid())
            (void)world.TryGet<AttributeSet>(FixtureEntity)->SetBase(id, static_cast<float>(action.Value));
        break;
    case AbilityPreviewControl::GrantTag:
    case AbilityPreviewControl::RevokeTag:
        if (const GameplayTagId tag = world.GetResource<GameplayTagRegistry>().FindTag(action.Name); tag.IsValid())
        {
            GameplayTagContainer& tags = *world.TryGet<GameplayTagContainer>(FixtureEntity);
            if (action.Control == AbilityPreviewControl::GrantTag)
                (void)tags.Grant(tag);
            else
                (void)tags.Revoke(tag);
        }
        break;
    case AbilityPreviewControl::TimeScale:
    {
        if (!world.HasComponent<ActorTimeLeases>(FixtureEntity))
            world.AddComponent<ActorTimeLeases>(FixtureEntity, ActorTimeLeases{});
        ActorTimeLeases& leases = *world.TryGet<ActorTimeLeases>(FixtureEntity);
        const LeaseOwner owner{ .Entity = FixtureEntity, .Key = kControlLeaseKey };
        ReleaseActorTimeLeases(leases, owner, tick);
        if (action.Value > 0.0 && action.Value != 1.0)
            (void)AcquireActorTimeLease(leases, owner, std::llround(action.Value * static_cast<double>(kActorTimeOne)),
                                        tick);
        break;
    }
    case AbilityPreviewControl::PlaceTarget:
    {
        const EntityId target = world.CreateEntity();
        world.AddComponent<AttributeSet>(target);
        const AttributeRegistry& attributes = world.GetResource<AttributeRegistry>();
        for (std::uint32_t id = 1; id <= attributes.Size(); ++id)
            (void)world.TryGet<AttributeSet>(target)->Add(AttributeId{ id }, attributes.DefaultBase(AttributeId{ id }));
        const Aim aim = AimOf(world, FixtureEntity);
        BodyDesc box;
        box.Shape = CollisionShape::MakeBox(Vec3d(kTargetHalfExtent, kTargetHalfExtent, kTargetHalfExtent));
        box.Position = aim.Origin + aim.Direction * static_cast<float>(action.Value);
        box.Motion = BodyMotion::Static;
        box.Layer = CollisionLayer::Static;
        box.UserData = PackEntity(target);
        (void)Schedule->Kit.Get<PhysicsStepSystem>()->GetSimulation().AddBody(box);
        record.Placed.push_back(box.Position);
        break;
    }
    }
}

AbilityPreviewTickRecord AbilityPreviewSession::RunTick(std::uint64_t tick)
{
    World& world = *Preview;
    AbilityPreviewTickRecord record;
    record.Tick = tick;
    std::span<InputActionValue> values = world.GetResource<InputActionState>().BeginTick(tick);
    for (const AbilityPreviewAction& action : Actions)
        if (action.Tick == tick)
        {
            Apply(action, tick, record);
            record.Applied.push_back(action);
        }
    for (std::size_t a = 0; a < values.size(); ++a)
        values[a].Flags = Held[a] ? InputActionFlags::Held : InputActionFlags::None;

    while (SimulationClock.GetTickIndex() < tick)
        SimulationClock.EndFixedTick();
    FixedLogicContext logic{ .Config = Schedule->Config,
                             .Runtime = Schedule->Runtime,
                             .Time = SimulationClock.BeginFixedTick(),
                             .Entities = world,
                             .Partitions = AllPartitions };
    Schedule->Kit.RunFixedLogic(logic);
    PhysicsContext stepped{ .Config = Schedule->Config,
                            .Runtime = Schedule->Runtime,
                            .Time = logic.Time,
                            .Entities = world,
                            .Partitions = AllPartitions };
    Schedule->Kit.RunPhysics(stepped);

    world.GetResource<AbilityTrace>().Records().ForEach([&](const AbilityTraceRecord& traced) {
        if (traced.Tick == tick && traced.Actor == FixtureEntity)
            record.Trace.push_back(traced);
    });
    for (const AbilityRecord& held : world.TryGet<ActiveAbilities>(FixtureEntity)->Records)
        if (held.State != AbilityRecordState::Free)
            record.Records.push_back(held);
    const AttributeRegistry& attributes = world.GetResource<AttributeRegistry>();
    const AttributeSet& set = *static_cast<const World&>(world).TryGet<AttributeSet>(FixtureEntity);
    for (std::uint32_t id = 1; id <= attributes.Size(); ++id)
        record.Attributes.push_back(set.GetCurrent(AttributeId{ id }));
    if (const ActorTime* time = static_cast<const World&>(world).TryGet<ActorTime>(FixtureEntity))
        record.ScaleQ16 = time->ScaleQ16;
    record.Pose = static_cast<const World&>(world).TryGet<LocalTransform>(FixtureEntity)->Value;
    const Aim aim = AimOf(world, FixtureEntity);
    record.AimOrigin = aim.Origin;
    record.AimDirection = aim.Direction;
    const ActiveAbilities& active = *static_cast<const World&>(world).TryGet<ActiveAbilities>(FixtureEntity);
    const AbilityLocals& locals = *static_cast<const World&>(world).TryGet<AbilityLocals>(FixtureEntity);
    for (std::size_t slot = 0; slot < kAbilityRecordCapacity; ++slot)
        if (active.Records[slot].State != AbilityRecordState::Free)
            for (const AbilityLocalValue& value : locals.Records[slot].Values)
                if (value.Set && value.Hit)
                    record.Hits.push_back({ .Point = value.Point, .Normal = value.Normal });
    return record;
}
