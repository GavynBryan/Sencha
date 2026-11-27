// Abilities seen from another machine: the authority's records and cue log
// travel as bytes into a client whose tag ids, ability ids and entity ids all
// differ, and the client presents them -- through loss, in order, once each,
// and within the loss bounds docs/gameplay/abilitykit.md states.

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityKit.h>
#include <abilities/AbilityPredictionJournal.h>
#include <abilities/AbilityPresentationSystem.h>
#include <abilities/AbilityWirePrecision.h>
#include <abilities/CuePresentationData.h>
#include <abilities/CuePresentationLibrary.h>
#include <app/EngineVocabulary.h>
#include <app/EngineVocabularyBindings.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <controller/LookOrientation.h>
#include <core/json/JsonParser.h>
#include <ecs/WorldComponentSchema.h>
#include <input/InputActionRegistry.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <input/InputActionVocabulary.h>
#include <movement/JumpState.h>
#include <movement/components/CharacterFacts.h>
#include <movement/components/CharacterMovement.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>
#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSet.h>
#include <time/ActorTimeLeases.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationRuntime.h>
#include <net/ReplicationSnapshot.h>
#include <time/ActorTimeSystem.h>
#include <time/SimClock.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/SimulationAuthority.h>
#include <world/SimulationTimeline.h>
#include <world/transform/TransformComponents.h>

#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityRequestQueue.h>
#include <CounterCostProvider.h>

#include <equipment/Equipment.h>
#include <equipment/EquipmentComponents.h>
#include <equipment/EquipmentKit.h>
#include <equipment/WornGrantEntries.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventoryKit.h>
#include <gtest/gtest.h>

#include <tuple>

#include <cstdlib>
#include <iostream>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <numeric>
#include <string>
#include <vector>

namespace
{
constexpr double kTickSeconds = 1.0 / 60.0;
constexpr std::uint8_t kFire = 0;
constexpr std::uint8_t kGlowAction = 1;

struct Shown
{
    std::vector<std::int64_t> Counts;

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        std::int64_t count = 0;
        (void)invocation.Arguments->TryGetInt(0, count);
        Counts.push_back(count);
        return VerbAdmission::Accepted;
    }

    [[nodiscard]] std::int64_t Total() const { return std::accumulate(Counts.begin(), Counts.end(), std::int64_t{ 0 }); }
};

struct Calls
{
    int Count = 0;
    int Held = 0;

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        ++Count;
        Held += invocation.Step == LeaseStep::Acquire ? 1 : invocation.Step == LeaseStep::Release ? -1 : 0;
        return VerbAdmission::Accepted;
    }
};

struct Glow
{
    int Held = 0;

    VerbAdmission Invoke(const VerbInvocation& invocation)
    {
        Held += invocation.Step == LeaseStep::Acquire ? 1 : -1;
        return VerbAdmission::Accepted;
    }
};

constexpr std::string_view kShot = R"({
    "input_default": { "action": "Input.PrimaryFire" },
    "phases": [ { "key": "p_fire", "enter": [ { "key": "o_shot", "op": "cue", "cue": "cue.shot",
                                                 "at": { "port": "aim.origin" } } ],
                  "then": [ { "key": "t_end", "to": "end" } ] } ] })";

constexpr std::string_view kAutomatic = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "phases": [ { "key": "p_fire", "while": { "every": 2, "first": "on_entry",
                    "ops": [ { "key": "o_muzzle", "op": "cue", "cue": "cue.muzzle", "at": { "port": "aim.origin" } } ] },
                  "then": [ { "key": "t_stop", "on": "release", "to": "end" } ] } ] })";

constexpr std::string_view kGlowing = R"({
    "input_default": { "action": "Input.Glow", "policy": "press_release" },
    "phases": [ { "key": "p_on", "present": [ { "key": "pr_glow", "cue": "cue.glow" } ],
                  "then": [ { "key": "t_off", "on": "release", "to": "end" } ] } ] })";

// One machine: the kit, the engine's vocabulary, and a presentation of the three
// cues that records what it was asked to show.
struct Machine
{
    void Build(const WorldComponentSchema& schema, std::initializer_list<const char*> ownTags)
    {
        schema.Apply(Entities);
        RegisterAbilityKit(Entities);
        InstallAuthoredVocabulary(Entities);
        GameplayTagRegistry& tags = Entities.GetResource<GameplayTagRegistry>();
        AttributeRegistry& attributes = Entities.GetResource<AttributeRegistry>();
        for (const char* name : ownTags)
        {
            (void)tags.RegisterTag(name);
            (void)attributes.RegisterAttribute(name);
        }
        for (const char* name : { "cue.shot", "cue.muzzle", "cue.glow", "State.Charged" })
            (void)tags.RegisterTag(name);
        if (const std::optional<GameplayTagId> firing = tags.RegisterTag("Weapon.Firing"))
            tags.SetReplicatedToAll(*firing, true);
        (void)attributes.RegisterAttribute("Energy", 0.0f, 100.0f, 100.0f);
        {
            VerbRegistrationScope verbs(*FindVerbRegistry(Entities), "test");
            VerbDefinition show;
            show.Name = "test.show";
            show.Scope = AuthoredScope::Cosmetic;
            show.Prediction = AuthoredPrediction::Presentation;
            DataFieldSchema count = MakeDataField(DataFieldKind::Int, "count", "Count");
            count.Required = false;
            count.Default = std::int64_t{ 1 };
            show.Arguments.Children = { std::move(count) };
            (void)verbs.Declare(std::move(show));
            VerbDefinition glow;
            glow.Name = "test.glow";
            glow.Kind = VerbKind::Lease;
            glow.Scope = AuthoredScope::Cosmetic;
            glow.Prediction = AuthoredPrediction::Presentation;
            (void)verbs.Declare(std::move(glow));
            VerbDefinition simulated;
            simulated.Name = "test.simulated";
            simulated.Kind = VerbKind::Lease;
            simulated.Prediction = AuthoredPrediction::Simulation;
            (void)verbs.Declare(std::move(simulated));
            VerbDefinition authorityOnly;
            authorityOnly.Name = "test.authority";
            (void)verbs.Declare(std::move(authorityOnly));
            EXPECT_TRUE(verbs.Commit());
        }
        EXPECT_TRUE(InstallEngineVocabulary(Entities));
        Actions.Rebuild(std::vector<InputActionDefinition>{ { .Name = "Input.PrimaryFire" }, { .Name = "Input.Glow" } });
        Entities.SetResource(InputActionVocabulary{ &Actions, 1 });
        Entities.AddResource<InputActionState>().Configure(2);

        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities));
        Bindings = std::make_unique<EngineVocabularyBindings>(Entities, Clock, *Verbs);
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.show"), Showings));
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.glow"), Glows));
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.simulated"), Simulated));
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.authority"), AuthorityOnly));
        Host = AbilityHost{ .Verbs = Verbs.get(), .Queries = Queries.get() };
        Presenter = std::make_unique<AbilityPresentationSystem>(Verbs.get());

        CuePresentationDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<std::string> errors;
        EXPECT_TRUE(CompileCuePresentation(*JsonParse(R"({ "cues": [
            { "key": "k_shot", "cue": "cue.shot", "once": [ { "verb": "test.show" } ] },
            { "key": "k_muzzle", "cue": "cue.muzzle",
              "once": [ { "verb": "test.show", "arguments": { "count": { "port": "count" } } } ] },
            { "key": "k_glow", "cue": "cue.glow", "sustain": [ { "lease": "test.glow" } ] } ] })"),
                                           declaration, dependencies, errors));
        Entities.GetResource<CuePresentationLibrary>().Declare("asset://cues/test.sdata", declaration, Entities, errors);
        EXPECT_TRUE(errors.empty()) << errors.front();
    }

    AbilityId Define(std::string_view json, std::string_view path)
    {
        AbilityDeclaration declaration;
        std::vector<AssetRef> dependencies;
        std::vector<AbilityDiagnostic> diagnostics;
        EXPECT_TRUE(CompileAbilityDeclaration(*JsonParse(json), declaration, dependencies, diagnostics));
        AbilityDefinition definition;
        EXPECT_TRUE(ResolveAbilityDeclaration(declaration, path, MakeAbilityResolveEnvironment(Entities), definition,
                                              diagnostics))
            << (diagnostics.empty() ? std::string() : diagnostics.front().Message);
        return Entities.GetResource<AbilityRegistry>().Define(path, std::move(definition));
    }

    World Entities;
    InputActionRegistry Actions;
    FixedSimulationLoop Clock{ kTickSeconds };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<AuthoredQueryDispatcher> Queries;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    std::vector<VerbBindingToken> Tokens;
    Shown Showings;
    Glow Glows;
    Calls Simulated;
    Calls AuthorityOnly;
    AbilityHost Host;
    AbilitySettings Settings;
    std::unique_ptr<AbilityPresentationSystem> Presenter;
};

class AbilityReplication : public ::testing::Test
{
protected:
    AbilityReplication()
    {
        ComponentRegistrar components(&Schema, nullptr, &Layout);
        RegisterEngineComponents(components);
        Schema.Seal();
        Layout.Seal();
        Authority.Build(Schema, {});
        // Names of its own first, so every tag id, and the ability ids below, differ.
        Client.Build(Schema, { "test.client.first", "test.client.second" });
        (void)Client.Define(kGlowing, "asset://abilities/client_only.sdata");
        for (int i = 0; i < 3; ++i)
            (void)Client.Entities.CreateEntity();

        Actor = Authority.Entities.CreateEntity();
        Authority.Entities.AddComponent<NetReplicated>(Actor);
        Authority.Entities.AddComponent<LocalTransform>(
            Actor, LocalTransform{ Transform3f{ Vec3d(4.0f, 0.0f, 0.0f), Quatf::Identity(), Vec3d::One() } });
        Authority.Entities.AddComponent<LookOrientation>(Actor, LookOrientation{});
        Authority.Entities.AddComponent<InputActionSourceRef>(Actor, InputActionSourceRef{});
        Authority.Entities.AddComponent<AbilityGrants>(Actor, AbilityGrants{});
        Authority.Entities.AddComponent<NetOwner>(Actor, NetOwner{ .Peer = kOwner });
        Authority.Entities.AddComponent<AttributeSet>(Actor, AttributeSet{});
    }

    void Grant(std::string_view json, std::string_view path, std::string key = "g")
    {
        const AbilityId ability = Authority.Define(json, path);
        // The client's own copy of the grant entry, as its prefab interns one.
        (void)Client.Entities.GetResource<AbilityGrantEntries>().Intern({ key, Client.Define(json, path), {} });
        const std::uint16_t entry = Authority.Entities.GetResource<AbilityGrantEntries>().Intern({ key, ability, {} });
        (void)GrantAbility(Authority.Entities, Actor, ability, AbilityGrantKind::Authored, entry);
    }

    // A moving character with nine grants, four attributes and two tags, as its owner is first sent it.
    void MeasureFullPawnsOwnerSeed(std::size_t grants);

    struct Delta
    {
        std::size_t Bits = 0;
        std::size_t Components = 0;
        std::size_t Empty = 0;
        std::size_t EmptyBits = 0;
        std::string EmptyNames;
    };

    Delta PublishMeasured();
    std::vector<std::uint64_t> PublishOwedElements(ComponentTypeId type);
    void MeasureMovingActorsDelta(bool wearing);
    void MeasureOwnerChange(std::string_view what, const std::function<void()>& change);

    void ShippingPawn()
    {
        Budget = kNetDefaultSnapshotBytes;
        for (int i = 0; i < 9; ++i)
            Grant(kShot, "asset://abilities/shot_" + std::to_string(i) + ".sdata", "g_" + std::to_string(i));
        AttributeSet& attributes = *Authority.Entities.TryGet<AttributeSet>(Actor);
        for (const char* name : { "Energy", "Health", "Ammo", "MoveSpeed" })
        {
            AttributeId id = Authority.Entities.GetResource<AttributeRegistry>().FindAttribute(name);
            if (!id.IsValid())
            {
                id = Authority.Entities.GetResource<AttributeRegistry>().RegisterAttribute(name);
                (void)Client.Entities.GetResource<AttributeRegistry>().RegisterAttribute(name);
            }
            (void)attributes.Add(id, 50.0f);
        }
        World& world = Authority.Entities;
        world.AddComponent<CharacterMovement>(Actor, CharacterMovement{ .Mode = LocomotionModeId{ 2 } });
        world.TryGet<KinematicState>(Actor)->Velocity = Vec3d(3.0f, -1.0f, 2.0f);
        *world.TryGet<SupportState>(Actor) = SupportState{ .Surface = {},
                                                           .ContactPoint = Vec3d(4.0f, -1.0f, 0.5f),
                                                           .Normal = Vec3d(0.0f, 1.0f, 0.0f),
                                                           .SurfaceVelocity = Vec3d(0.5f, 0.0f, 0.0f),
                                                           .Kind = SupportKind::Stable };
        world.TryGet<JumpState>(Actor)->CooldownRemaining = 0.25f;
        world.AddComponent<NetSpawnPrefab>(Actor, NetSpawnPrefab{ .Scene = AssetId{ 0xC3DD45711F65F6EBull } });
        world.AddComponent<NetDrivenBy>(Actor, NetDrivenBy{ .Peer = kOwner });
        world.TryGet<LookOrientation>(Actor)->Yaw = 1.25f;
        Step();
        GameplayTagContainer& tags = *Authority.Entities.TryGet<GameplayTagContainer>(Actor);
        const GameplayTagRegistry& names = Authority.Entities.GetResource<GameplayTagRegistry>();
        (void)tags.Grant(names.FindTag("State.Charged"));
        (void)tags.Grant(names.FindTag("Weapon.Firing"));
    }

    // Gives the pawn `records` entries of an item that occupies one slot each,
    // and, when `worn`, an EquipmentState wearing every one of them: its
    // largest legal size at kEquipmentRecordCapacity.
    std::vector<EntityId> Wear(std::size_t records, bool worn)
    {
        std::vector<EntityId> entries;
        ItemId item;
        EquipmentLayoutId body;
        // The authority last, so the ids kept are its own.
        for (Machine* machine : { &Client, &Authority })
        {
            World& world = machine->Entities;
            InstallInventoryVocabulary(world);
            InstallEquipmentVocabulary(world);
            EquipmentLayout layout;
            for (std::size_t i = 0; i < kEquipmentRecordCapacity; ++i)
            {
                EquipmentSlot slot;
                slot.Key = "s." + std::to_string(i);
                layout.Slots.push_back(std::move(slot));
            }
            body = world.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/many.sdata", std::move(layout));
            item = world.GetResource<ItemRegistry>().Define("asset://items/charm.sdata",
                                                            ItemDefinition{ .Name = "Charm", .Occupies = { "s" } });
        }
        World& world = Authority.Entities;
        world.AddComponent<ItemContainer>(Actor, ItemContainer{});
        EquipmentRecordList wearing{ .Layout = body, .NextKey = 1, .Records = {} };
        for (std::size_t i = 0; i < records; ++i)
        {
            const EntityId entry = Inventory(world, FixedSimTime{}).Give(Actor, item, 1).Entry;
            entries.push_back(entry);
            wearing.Records.push_back({ .Key = wearing.NextKey++, .Definition = item, .Slots = 1u << i,
                                        .Primary = static_cast<std::uint8_t>(i),
                                        .Source = MakePersistableRef(world, entry),
                                        .Derivation = EquipmentDerivation::Pending });
        }
        if (worn)
            SetEquipment(world, Actor, wearing);
        return entries;
    }

    [[nodiscard]] EntityId Mirror(EntityId entity) const { return ClientIds.TryResolve(Identity.TryFind(entity)); }

    // The client's record of the authority's record `key`, or null.
    [[nodiscard]] const EquipmentRecord* ClientRecord(std::uint32_t key) const
    {
        const std::span<const EquipmentRecord> records = EquipmentRecordsOf(Client.Entities, Remote());
        const auto found = std::ranges::find(records, key, &EquipmentRecord::Key);
        return found != records.end() ? &*found : nullptr;
    }

    // One authority tick.
    void Step(std::vector<InputActionEdge> edges = {})
    {
        World& world = Authority.Entities;
        InputActionState& state = world.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(Now);
        for (const InputActionEdge& edge : edges)
        {
            Held[edge.Action] = edge.Kind == InputEdgeKind::Press;
            (void)state.TickEdgeStorage().Append(edge);
        }
        for (std::size_t a = 0; a < values.size(); ++a)
            values[a].Flags = Held[a] ? InputActionFlags::Held : InputActionFlags::None;
        AdvanceActorTime(world, Now);
        RunAbilityPass(world, Authority.Host, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now }, nullptr,
                       Authority.Settings);
        ++Now;
    }

    void Steps(int count)
    {
        for (int i = 0; i < count; ++i)
            Step();
    }

    // A snapshot of the authority's state, written; applied on the client unless lost.
    std::vector<std::byte> Write()
    {
        Changes.Update(Authority.Entities, Layout, Identity, ++Generation);
        SnapshotWriteRequest write;
        write.Changes = &Changes;
        write.Layout = &Layout;
        write.Peer = &Peer;
        write.Tick = Now;
        write.Sequence = Peer.NextSnapshotSequence();
        write.OwnerPeer = ClientPeer;
        write.CommandAck = Now > 0 ? Now - 1 : 0;
        LastWrite = ReplicationWriteSnapshot(write, std::span(Scratch).subspan(0, Budget));
        EXPECT_TRUE(LastWrite.Ok);
        return { Scratch.begin(), Scratch.begin() + static_cast<std::ptrdiff_t>(LastWrite.BytesWritten) };
    }

    void Apply(const std::vector<std::byte>& bytes)
    {
        SnapshotApplyRequest apply;
        apply.Target = &Client.Entities;
        apply.Schema = &Schema;
        apply.Layout = &Layout;
        apply.Identity = &ClientIds;
        const SnapshotApplyResult applied = ReplicationApplySnapshot(apply, bytes);
        ASSERT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);
        // As the engine does after every snapshot.
        Client.Entities.GetResource<AbilityPredictionJournal>().Reconcile(Client.Entities, applied.CommandAck,
                                                                         Client.Verbs.get());
        Ack.Observe(applied.Sequence);
        Peer.Acknowledge(Ack);
    }

    // A publish, delivered and presented on the client at the authority's tick, or lost.
    std::size_t Publish(bool delivered = true)
    {
        const std::vector<std::byte> bytes = Write();
        if (delivered)
        {
            Apply(bytes);
            Client.Presenter->Present(Client.Entities, Now);
        }
        return bytes.size();
    }

    [[nodiscard]] EntityId Remote() const { return ClientIds.TryResolve(Identity.TryFind(Actor)); }

    [[nodiscard]] const AbilityRecord& AuthorityRecord() const
    {
        return Authority.Entities.TryGetResource<AbilityActivations>()->Record(
            *static_cast<const World&>(Authority.Entities).TryGet<ActiveAbilities>(Actor), 0);
    }

    struct Tail
    {
        std::int64_t Fired = 0;
        std::int64_t Shown = 0;
    };

    // An automatic fired with every publish delivered, a pulse no publish has
    // carried yet, then the release and the first `lost` publishes after it lost.
    Tail TailAfterLosing(int lost)
    {
        Grant(kAutomatic, "asset://abilities/automatic.sdata");
        Step();
        (void)Publish();
        Step({ { kFire, InputEdgeKind::Press } });
        Run(6);
        Steps(2);
        Step({ { kFire, InputEdgeKind::Release } });
        EXPECT_EQ(AuthorityRecord().State, AbilityRecordState::Lingering);
        EXPECT_GT(static_cast<std::int64_t>(AuthorityRecord().Pulse), Client.Showings.Total());
        int publishes = 0;
        const auto fired = static_cast<std::int64_t>(AuthorityRecord().Pulse);
        while (AuthorityRecord().State != AbilityRecordState::Free)
        {
            Step();
            if (Now % kNetDefaultSnapshotInterval == 0)
                (void)Publish(publishes++ >= lost);
        }
        LingerPublishes = publishes;
        Run(4);
        return { fired, Client.Showings.Total() };
    }

    int LingerPublishes = 0;

    // The client as the actor's owner, predicting it on the authority's numbering.
    void Predict()
    {
        ClientPeer = kOwner;
        Step();
        (void)Publish();
        Client.Entities.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = 0 });
        Client.Entities.SetResource(PredictedSimulation{ .Entity = Remote(), .CommandTickOffset = 0 });
        Client.Entities.AddComponent<InputActionSourceRef>(Remote(), InputActionSourceRef{});
        ClientNow = Now;
    }

    // A client tick at the head of its timeline, its input queued for the authority,
    // which runs it `Lag` ticks later; a publish every second authority tick.
    // `heard`, when given, is what the authority hears for the tick instead.
    void Lockstep(std::vector<InputActionEdge> edges = {},
                  std::optional<std::vector<InputActionEdge>> heard = std::nullopt)
    {
        ClientTick(edges);
        Commands.push_back(heard ? std::move(*heard) : std::move(edges));
        while (ClientNow - Now > kLag)
        {
            Step(std::move(Commands.front()));
            Commands.erase(Commands.begin());
            if (Now % kNetDefaultSnapshotInterval == 0)
                (void)Publish();
        }
    }

    // The client's pass and presentation alone, at the head of its timeline.
    void ClientTick(const std::vector<InputActionEdge>& edges)
    {
        World& world = Client.Entities;
        InputActionState& state = world.GetResource<InputActionState>();
        std::span<InputActionValue> values = state.BeginTick(ClientNow);
        for (const InputActionEdge& edge : edges)
        {
            ClientHeld[edge.Action] = edge.Kind == InputEdgeKind::Press;
            (void)state.TickEdgeStorage().Append(edge);
        }
        for (std::size_t a = 0; a < values.size(); ++a)
            values[a].Flags = ClientHeld[a] ? InputActionFlags::Held : InputActionFlags::None;
        AdvanceActorTime(world, ClientNow);
        RunAbilityPass(world, Client.Host, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = ClientNow }, nullptr,
                       Client.Settings);
        Client.Presenter->Present(world, ClientNow);
        ++ClientNow;
    }

    void Locksteps(int count)
    {
        for (int i = 0; i < count; ++i)
            Lockstep();
    }

    static constexpr std::uint64_t kLag = 4;
    std::uint64_t ClientNow = 0;
    bool ClientHeld[2] = {};
    std::vector<std::vector<InputActionEdge>> Commands;

    struct RecordsHeld
    {
        std::array<AbilityRecord, kAbilitySlotCapacity> Records;
    };

    [[nodiscard]] RecordsHeld RemoteRecords() const { return { AbilityRecordsOf(Client.Entities, Remote()) }; }

    // Steps `ticks` authority ticks, publishing every second one as the default cadence does.
    void Run(int ticks, bool delivered = true)
    {
        for (int i = 0; i < ticks; ++i)
        {
            Step();
            if (Now % kNetDefaultSnapshotInterval == 0)
                (void)Publish(delivered);
        }
    }

    WorldComponentSchema Schema;
    ReplicationLayout Layout;
    Machine Authority;
    Machine Client;
    ReplicationAuthorityIdentity Identity;
    ReplicationChangeStore Changes;
    ReplicationPeerState Peer;
    ReplicationClientIdentity ClientIds;
    NetSnapshotAck Ack;
    std::uint64_t Generation = 0;
    std::vector<std::byte> Scratch = std::vector<std::byte>(64 * 1024);
    // How much of Scratch one snapshot may fill; a shipping session's is kNetDefaultSnapshotBytes.
    std::size_t Budget = Scratch.size();
    SnapshotWriteResult LastWrite;
    EntityId Actor;
    std::uint64_t Now = 0;
    bool Held[2] = {};
    static constexpr std::uint32_t kOwner = 1;
    // Which peer the client is: the actor's owner, or anyone else.
    std::uint32_t ClientPeer = 2;
};
}

TEST_F(AbilityReplication, RecordsArriveInTheClientsOwnNames)
{
    Grant(kAutomatic, "asset://abilities/automatic.sdata");
    Step();
    (void)Publish();
    Step({ { kFire, InputEdgeKind::Press } });
    Steps(4);
    (void)Publish();
    ASSERT_TRUE(Remote().IsValid());
    ASSERT_NE(Remote(), Actor) << "the same actor is a different entity here";
    const AbilityRecord record = RemoteRecords().Records[0];
    EXPECT_EQ(record.State, AbilityRecordState::Live);
    EXPECT_EQ(record.Key.Ability, Client.Entities.GetResource<AbilityRegistry>().Find("asset://abilities/automatic.sdata"))
        << "named by the client's id for the same ability";
    EXPECT_NE(record.Key.Ability,
              Authority.Entities.GetResource<AbilityRegistry>().Find("asset://abilities/automatic.sdata"));
    EXPECT_EQ(record.Pulse, 3u);
    // The point as its wire precision names it.
    EXPECT_NEAR(record.LastPoint.X, 4.0f, 2.0f * kNetPositionRange / ((1u << kAbilityHitPointBits) - 1u))
        << "where the last pulse was";
}

TEST_F(AbilityReplication, ShortActivationPresentsRemotelyThroughLinger)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Step();
    (void)Publish();
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    Step();
    (void)Publish();
    EXPECT_EQ(RemoteRecords().Records[0].State, AbilityRecordState::Lingering) << "it began and ended between publishes";
    ASSERT_EQ(Client.Showings.Counts.size(), 1u) << "and its shot still showed";
    Run(20);
    EXPECT_EQ(Client.Showings.Counts.size(), 1u) << "once";
}

TEST_F(AbilityReplication, AutomaticShotCountArrivesUnderLoss)
{
    Grant(kAutomatic, "asset://abilities/automatic.sdata");
    Step();
    (void)Publish();
    Step({ { kFire, InputEdgeKind::Press } });
    for (int i = 0; i < 40; ++i)
    {
        Step();
        if (Now % kNetDefaultSnapshotInterval == 0)
            (void)Publish(Now % 6 != 0);
    }
    Step({ { kFire, InputEdgeKind::Release } });
    const std::uint32_t fired = AuthorityRecord().Pulse;
    Run(4);
    EXPECT_EQ(Client.Showings.Total(), static_cast<std::int64_t>(fired))
        << "every pulse, though a third of the publishes were lost";
    EXPECT_LT(Client.Showings.Counts.size(), static_cast<std::size_t>(fired)) << "folded into fewer occurrences";
}

// An ended record lingers for LingerTicks, carried by every publish in that time:
// its last pulses are lost only if all of those are.
TEST_F(AbilityReplication, PulseCountLossBoundIsExact)
{
    const Tail tail = TailAfterLosing(3);
    EXPECT_EQ(LingerPublishes, 4) << "the linger spans four publishes at the defaults";
    EXPECT_EQ(tail.Shown, tail.Fired) << "three lost, and the fourth carried the last pulse";
}

TEST_F(AbilityReplication, PulseCountLossBoundIsExactWhenTheWholeLingerIsLost)
{
    const Tail tail = TailAfterLosing(4);
    EXPECT_LT(tail.Shown, tail.Fired) << "every publish of the linger lost: the last pulse is gone";
}

TEST_F(AbilityReplication, RingLossBoundIsExact)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Step();
    (void)Publish();
    for (std::size_t shot = 0; shot <= kAbilityCueLogCapacity; ++shot)
    {
        Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
        if (Now % kNetDefaultSnapshotInterval == 0)
            (void)Publish(false);
    }
    (void)Publish();
    EXPECT_EQ(Client.Showings.Counts.size(), kAbilityCueLogCapacity)
        << "the ninth shot no publish carried displaced the first";
}

TEST_F(AbilityReplication, OutOfOrderSnapshotsDoNotDuplicateOrRewind)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Step();
    (void)Publish();
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    const std::vector<std::byte> early = Write();
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    const std::vector<std::byte> late = Write();
    Apply(late);
    Client.Presenter->Present(Client.Entities, Now);
    ASSERT_EQ(Client.Showings.Counts.size(), 2u);
    Apply(early);
    Client.Presenter->Present(Client.Entities, Now);
    Apply(late);
    Client.Presenter->Present(Client.Entities, Now);
    EXPECT_EQ(Client.Showings.Counts.size(), 2u) << "an older state shows nothing again, and the newer one nothing twice";
}

TEST_F(AbilityReplication, SequenceWrapPresentsInOrder)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Authority.Entities.TryGet<AbilityCueLog>(Actor)->NextSeq = 0xFFFE;
    Step();
    (void)Publish();
    for (int shot = 0; shot < 4; ++shot)
    {
        Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
        (void)Publish();
    }
    EXPECT_EQ(Client.Showings.Counts.size(), 4u) << "each shot once, across the serial's wrap";
}

TEST_F(AbilityReplication, LateJoinerSeesSustainedButNoOldShots)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Grant(kGlowing, "asset://abilities/glowing.sdata");
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    Steps(static_cast<int>(kAbilityCueWindowTicks) + 4);
    Step({ { kGlowAction, InputEdgeKind::Press } });
    Step();
    (void)Publish();
    EXPECT_TRUE(Client.Showings.Counts.empty()) << "a shot from before it arrived is not shown";
    EXPECT_EQ(Client.Glows.Held, 1) << "what is still going on is";
    Step({ { kGlowAction, InputEdgeKind::Release } });
    Run(2);
    EXPECT_EQ(Client.Glows.Held, 0);
}

TEST_F(AbilityReplication, CueBandwidthPerFiringActorIsMeasured)
{
    Grant(kAutomatic, "asset://abilities/automatic.sdata");
    Step();
    (void)Publish();
    const auto spend = [&](bool firing) {
        if (firing)
            Step({ { kFire, InputEdgeKind::Press } });
        std::size_t bytes = 0;
        for (int i = 0; i < 60; ++i)
        {
            // The aim sweeps, so every pulse lands somewhere new.
            Authority.Entities.TryGet<LocalTransform>(Actor)->Value.Position.Z += 0.05f;
            Step();
            if (Now % kNetDefaultSnapshotInterval == 0)
                bytes += Publish();
        }
        if (firing)
            Step({ { kFire, InputEdgeKind::Release } });
        Run(20);
        return bytes;
    };
    const std::size_t moving = spend(false);
    const std::size_t firing = spend(true);
    const std::size_t firingOverMoving = firing - moving;
    RecordProperty("moving_bytes_per_second", static_cast<int>(moving));
    RecordProperty("firing_bytes_per_second", static_cast<int>(firing));
    RecordProperty("cue_bytes_per_second", static_cast<int>(firingOverMoving));
    std::cout << "bytes per second for one moving actor, to one peer: " << moving << "\n"
              << "bytes per second for one firing actor, to one peer: " << firing << "\n"
              << "of which firing adds over moving: " << firingOverMoving << "\n";
    EXPECT_LE(moving, 1150u) << "a moving actor's traffic to one peer";
    EXPECT_LE(firing, 1730u) << "a firing actor's traffic to one peer";
}

constexpr std::string_view kCharging = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "owned_tags": [ "Weapon.Firing", "State.Charged" ],
    "phases": [ { "key": "p_fire", "then": [ { "key": "t_stop", "on": "release", "to": "end" } ] } ] })";

TEST_F(AbilityReplication, TheOwnerIsSentItsActorsGrantsAttributesAndTags)
{
    ClientPeer = kOwner;
    Grant(kCharging, "asset://abilities/charging.sdata");
    const AttributeId energy = Authority.Entities.GetResource<AttributeRegistry>().FindAttribute("Energy");
    (void)Authority.Entities.TryGet<AttributeSet>(Actor)->Add(energy, 70.0f);
    Step({ { kFire, InputEdgeKind::Press } });
    (void)Publish();

    const World& client = Client.Entities;
    const std::span<const AbilityGrant> grants = AbilityGrantsOf(client, Remote());
    ASSERT_EQ(grants.size(), 1u);
    EXPECT_EQ(grants[0].Ability, (*client.TryGetResource<AbilityRegistry>()).Find("asset://abilities/charging.sdata"));
    EXPECT_NE(grants[0].Entry, kAbilityNoGrantEntry) << "bound to the client's own entry for the same grant";
    const AttributeId clientEnergy = (*client.TryGetResource<AttributeRegistry>()).FindAttribute("Energy");
    ASSERT_NE(clientEnergy, energy) << "the machines must number attributes differently for this to prove anything";
    EXPECT_FLOAT_EQ(client.TryGet<AttributeSet>(Remote())->GetCurrent(clientEnergy), 70.0f);
    const GameplayTagContainer& tags = *client.TryGet<GameplayTagContainer>(Remote());
    const GameplayTagRegistry& names = (*client.TryGetResource<GameplayTagRegistry>());
    EXPECT_TRUE(tags.HasExact(names.FindTag("State.Charged")));
    EXPECT_TRUE(tags.HasExact(names.FindTag("Weapon.Firing")));
}

TEST_F(AbilityReplication, TaggedFiringReplicatesToRemotes)
{
    Grant(kCharging, "asset://abilities/charging.sdata");
    Step({ { kFire, InputEdgeKind::Press } });
    (void)Publish();
    const World& client = Client.Entities;
    const GameplayTagRegistry& names = (*client.TryGetResource<GameplayTagRegistry>());
    const GameplayTagContainer* tags = client.TryGet<GameplayTagContainer>(Remote());
    ASSERT_NE(tags, nullptr);
    EXPECT_TRUE(tags->HasExact(names.FindTag("Weapon.Firing"))) << "declared replicated to all";
    EXPECT_FALSE(tags->HasExact(names.FindTag("State.Charged"))) << "the owner's alone";
    const AbilityGrants* grants = client.TryGet<AbilityGrants>(Remote());
    EXPECT_TRUE(grants == nullptr || grants->Count == 0) << "grants are the owner's";

    Step({ { kFire, InputEdgeKind::Release } });
    Run(2);
    EXPECT_FALSE(client.TryGet<GameplayTagContainer>(Remote())->HasExact(names.FindTag("Weapon.Firing")))
        << "and it goes when the firing does";
}

TEST_F(AbilityReplication, ARemoteActorRunsAtTheScaleItsLeasesSay)
{
    Step();
    (void)Publish();
    Authority.Entities.AddComponent<ActorTimeLeases>(Actor, ActorTimeLeases{});
    ActorTimeLeases& leases = *Authority.Entities.TryGet<ActorTimeLeases>(Actor);
    ASSERT_TRUE(AcquireActorTimeLease(leases, LeaseOwner{ .Entity = Actor, .Key = 7 }, kActorTimeOne / 2, Now));
    Run(4);
    EXPECT_EQ(ActorScaleOnTick(Client.Entities, Remote(), Now), kActorTimeOne / 2)
        << "a dilated actor animates at its scale wherever it is seen";
}

TEST_F(AbilityReplication, SameTickStartsGetDistinctKeys)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Step();
    (void)Publish();
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release },
           { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    (void)Publish();
    std::vector<std::uint64_t> identities;
    for (const AbilityRecord& record : AbilityRecordsOf(Authority.Entities, Actor))
        if (record.State != AbilityRecordState::Free)
            identities.push_back(record.Identity);
    ASSERT_EQ(identities.size(), 2u) << "two presses in one tick are two activations";
    EXPECT_NE(identities[0], identities[1]) << "named apart by where each press was in the tick";
    for (const std::uint64_t identity : identities)
        EXPECT_TRUE(std::ranges::any_of(RemoteRecords().Records,
                                        [&](const AbilityRecord& record) { return record.Identity == identity; }))
            << "and the client names each the same way, in its own ability ids";
    EXPECT_EQ(Client.Showings.Counts.size(), 2u) << "both shots shown";
}

constexpr std::string_view kLevelled = R"({
    "input_default": { "action": "Input.PrimaryFire", "policy": "press_release" },
    "prediction": "%s",
    "phases": [ { "key": "p_hold", "hold": [ { "key": "o_sim", "lease": "test.simulated" } ],
                  "enter": [ { "key": "o_auth", "op": "invoke", "verb": "test.authority" } ],
                  "then": [ { "key": "t_off", "on": "release", "to": "end" } ] } ] })";

std::string Levelled(std::string_view level)
{
    std::string json(kLevelled);
    json.replace(json.find("%s"), 2, level);
    return json;
}

TEST_F(AbilityReplication, OwnerDoesNotPresentPredictedCuesTwice)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Predict();
    Lockstep({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    ASSERT_EQ(Client.Showings.Counts.size(), 1u) << "shown on the press, before the authority has heard of it";
    Locksteps(20);
    EXPECT_EQ(Authority.Showings.Counts.size(), 0u) << "the authority presents nothing here: nothing presents there";
    EXPECT_EQ(static_cast<const World&>(Authority.Entities).TryGet<AbilityCueLog>(Actor)->NextSeq, 2u)
        << "the authority logged the same shot";
    EXPECT_EQ(Client.Showings.Counts.size(), 1u) << "and its entry for it was not shown again";
}

TEST_F(AbilityReplication, ServerExtraPulsesPresentLateOnTheOwner)
{
    Grant(kAutomatic, "asset://abilities/automatic.sdata");
    Predict();
    Lockstep({ { kFire, InputEdgeKind::Press } });
    Locksteps(5);
    // The client lets go; the authority hears it four ticks late and fires twice more.
    Lockstep({ { kFire, InputEdgeKind::Release } }, std::vector<InputActionEdge>{});
    Locksteps(3);
    Lockstep({}, std::vector<InputActionEdge>{ { kFire, InputEdgeKind::Release } });
    Locksteps(24);
    std::int64_t authorityPulses = 0;
    Authority.Entities.GetResource<AbilityTrace>().Records().ForEach([&](const AbilityTraceRecord& record) {
        authorityPulses += record.Event == AbilityTraceEvent::Cue ? 1 : 0;
    });
    EXPECT_GT(authorityPulses, 4) << "the authority ran longer than the guess";
    EXPECT_EQ(Client.Showings.Total(), authorityPulses) << "the owner shows the pulses it did not guess, late";
}

TEST_F(AbilityReplication, RefusedPredictionEndsSustainedPresentation)
{
    Grant(kGlowing, "asset://abilities/glowing.sdata");
    Predict();
    Lockstep({ { kGlowAction, InputEdgeKind::Press } }, std::vector<InputActionEdge>{});
    ASSERT_EQ(Client.Glows.Held, 1) << "the guess glows at once";
    Locksteps(12);
    EXPECT_EQ(Client.Glows.Held, 0) << "the authority started nothing for that press, so the glow goes";
    EXPECT_EQ(Client.Entities.GetResource<AbilityPredictionJournal>().Size(), 0u);
    Locksteps(static_cast<int>(Client.Settings.LingerTicks) + 2);
    EXPECT_EQ(Client.Entities.GetResource<AbilityActivations>().Stats().Live, 0u) << "a refused guess gives its slot back";
}

TEST_F(AbilityReplication, SnapshotsReuseAnActivationAndFreeOneTheAuthorityEnded)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Step();
    (void)Publish();
    Step({ { kFire, InputEdgeKind::Press }, { kFire, InputEdgeKind::Release } });
    (void)Publish();
    const AbilityActivations& pool = Client.Entities.GetResource<AbilityActivations>();
    ASSERT_EQ(pool.Stats().Live, 1u);
    const std::uint64_t allocated = pool.Stats().Allocated;
    (void)Publish();
    (void)Publish();
    EXPECT_EQ(pool.Stats().Live, 1u) << "a slot the wire still names keeps its activation";
    EXPECT_EQ(pool.Stats().Allocated, allocated);
    Run(40);
    EXPECT_EQ(pool.Stats().Live, 0u) << "the record the wire no longer names is freed";
}

TEST_F(AbilityReplication, AGuessRunsOnlyWhatItsPredictionLevelAllows)
{
    Grant(Levelled("presentation"), "asset://abilities/presented.sdata");
    Predict();
    Lockstep({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(Client.Simulated.Held, 0) << "a simulation operation waits for the authority at presentation level";
    EXPECT_EQ(Client.AuthorityOnly.Count, 0) << "an authority-only one never runs on a client";
    Locksteps(10);
    EXPECT_EQ(Authority.Simulated.Held, 1);
    EXPECT_EQ(Authority.AuthorityOnly.Count, 1);
    Lockstep({ { kFire, InputEdgeKind::Release } });
    Locksteps(10);
}

TEST_F(AbilityReplication, ASimulationLevelGuessHoldsWhatTheAuthorityWill)
{
    Grant(Levelled("simulation"), "asset://abilities/simulated.sdata");
    Predict();
    Lockstep({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(Client.Simulated.Held, 1) << "held on the press, ahead of the authority";
    EXPECT_EQ(Client.AuthorityOnly.Count, 0);
    Locksteps(10);
    EXPECT_EQ(Client.Simulated.Held, 1) << "a confirmed guess keeps running";
    Lockstep({ { kFire, InputEdgeKind::Release } });
    EXPECT_EQ(Client.Simulated.Held, 0) << "and lets go when its input does";
    Locksteps(10);
    EXPECT_EQ(Client.Simulated.Held, 0);
}

namespace
{
template <typename Held>
std::size_t CountRecords(const Held& held, bool predicted)
{
    return static_cast<std::size_t>(std::ranges::count_if(held.Records, [&](const AbilityRecord& record) {
        return record.State == AbilityRecordState::Live && record.Predicted == predicted;
    }));
}

AbilityCueEntry CueAt(GameplayTagId cue, std::uint64_t tick, EntityId target)
{
    return { .Cue = cue, .Activation = 77, .Op = 0, .Tick = tick, .Target = target };
}
}

TEST_F(AbilityReplication, AClientsReleaseLeavesTheAuthoritysRecordAlone)
{
    Grant(kGlowing, "asset://abilities/glowing.sdata");
    Predict();
    ActiveAbilities& active = *Client.Entities.TryGet<ActiveAbilities>(Remote());
    AbilityRecord& word = Client.Entities.GetResource<AbilityActivations>().Claim(active, 0, Remote()).Record;
    word.State = AbilityRecordState::Live;
    word.Key = { .CommandTick = ClientNow, .Ability = Client.Entities.GetResource<AbilityRegistry>().Find(
                                               "asset://abilities/glowing.sdata") };
    word.Grant = AbilityGrantsOf(Client.Entities, Remote())[0].Id;
    word.TriggerAction = kGlowAction;
    word.Identity = 99;
    ClientHeld[kGlowAction] = true;
    ClientTick({ { kGlowAction, InputEdgeKind::Release } });
    const AbilityRecord& after =
        Client.Entities.GetResource<AbilityActivations>().Record(*Client.Entities.TryGet<ActiveAbilities>(Remote()), 0);
    EXPECT_EQ(after.State, AbilityRecordState::Live) << "the authority's record is only presented here";
    EXPECT_FALSE(after.Released);
}

TEST_F(AbilityReplication, AGuessCancelsOnlyGuesses)
{
    Grant(R"({
        "input_default": { "action": "Input.Glow", "policy": "press_release" },
        "prediction": "none",
        "owned_tags": ["State.Charged"],
        "phases": [ { "key": "p_on", "then": [ { "key": "t_off", "on": "release", "to": "end" } ] } ] })",
          "asset://abilities/charged.sdata", "g_charged");
    Grant(R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "cancel_tags": ["State.Charged"],
        "phases": [ { "key": "p_fire", "then": [ { "key": "t_end", "to": "end" } ] } ] })",
          "asset://abilities/discharge.sdata", "g_discharge");
    Predict();
    Lockstep({ { kGlowAction, InputEdgeKind::Press } });
    Locksteps(12);
    ASSERT_EQ(CountRecords(RemoteRecords(), false), 1u) << "the authority's charge, which the client does not guess";
    ClientTick({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(CountRecords(RemoteRecords(), false), 1u)
        << "the guessed discharge leaves the authority's charge for the authority to cancel";
}

TEST_F(AbilityReplication, AFourthGuessFitsBesideThreeConfirmedOnes)
{
    Grant(R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "instances": 4,
        "phases": [ { "key": "p_wait", "then": [ { "key": "t_end", "on": "timeout", "ticks": 400, "to": "end" } ] } ] })",
          "asset://abilities/lasting.sdata");
    Predict();
    for (int start = 0; start < 3; ++start)
    {
        Lockstep({ { kFire, InputEdgeKind::Press } });
        Lockstep({ { kFire, InputEdgeKind::Release } });
        Locksteps(12);
    }
    ASSERT_EQ(CountRecords(RemoteRecords(), true), 3u);
    ASSERT_EQ(CountRecords(RemoteRecords(), false), 3u) << "each guess confirmed: the authority's copy beside it";

    Lockstep({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(CountRecords(RemoteRecords(), true), 4u) << "a fourth activation the authority will admit is guessed";
    Lockstep({ { kFire, InputEdgeKind::Release } });
    Locksteps(12);
    EXPECT_EQ(CountRecords(RemoteRecords(), true), 4u);
    EXPECT_EQ(CountRecords(RemoteRecords(), false), 4u) << "and kept beside the authority's four";

    Lockstep({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(CountRecords(RemoteRecords(), true), 4u) << "a fifth is refused here as the authority refuses it";
}

TEST_F(AbilityReplication, TwoGrantsOfOneAbilityAreNamedApartOnEveryMachine)
{
    constexpr std::string_view kWatch = R"({
        "input_default": { "action": "Input.PrimaryFire", "consume": "observe" },
        "instances": 2,
        "phases": [ { "key": "p_wait", "then": [ { "key": "t_end", "on": "timeout", "ticks": 30, "to": "end" } ] } ] })";
    Grant(kWatch, "asset://abilities/watch.sdata", "g_left");
    Grant(kWatch, "asset://abilities/watch.sdata", "g_right");
    Step();
    (void)Publish();
    Step({ { kFire, InputEdgeKind::Press } });
    (void)Publish();
    std::vector<std::uint64_t> identities;
    for (const AbilityRecord& record : AbilityRecordsOf(Authority.Entities, Actor))
        if (record.State == AbilityRecordState::Live)
            identities.push_back(record.Identity);
    ASSERT_EQ(identities.size(), 2u);
    EXPECT_NE(identities[0], identities[1]);
    for (const std::uint64_t identity : identities)
        EXPECT_TRUE(std::ranges::any_of(RemoteRecords().Records,
                                        [&](const AbilityRecord& record) { return record.Identity == identity; }))
            << "the client names each activation as the authority does";
}

TEST(AbilityCueOccurrence, NamesEachTargetAndEachRepeat)
{
    const GameplayTagId cue{ 3 };
    const AbilityCueEntry first = CueAt(cue, 10, EntityId{ 1, 1 });
    AbilityCueEntry other = first;
    other.Target = EntityId{ 2, 1 };
    AbilityCueEntry again = first;
    again.Ordinal = 1;
    AbilityCueEntry later = first;
    later.Seq = 9;
    later.Point = Vec3d(1.0f, 2.0f, 3.0f);
    EXPECT_NE(AbilityCueOccurrence(first), AbilityCueOccurrence(other))
        << "one op striking two targets in a tick is two occurrences";
    EXPECT_NE(AbilityCueOccurrence(first), AbilityCueOccurrence(again)) << "and striking one target twice is two";
    EXPECT_EQ(AbilityCueOccurrence(first), AbilityCueOccurrence(later))
        << "a guess names its occurrence as the authority does, whatever its serial or place";
}

TEST_F(AbilityReplication, AGuessedCueHidesOnlyTheAuthorityEntryItGuessed)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    Predict();
    World& client = Client.Entities;
    const GameplayTagId shot = client.GetResource<GameplayTagRegistry>().FindTag("cue.shot");
    const EntityId struck = client.CreateEntity();
    const EntityId missed = client.CreateEntity();
    client.AddComponent<AbilityPredictedCues>(Remote());
    (void)AppendAbilityCue(client.TryGet<AbilityPredictedCues>(Remote())->Log, CueAt(shot, ClientNow, struck));
    Client.Presenter->Present(client, ClientNow);
    ASSERT_EQ(Client.Showings.Counts.size(), 1u) << "the guess is shown at once";

    AbilityCueLog& word = *client.TryGet<AbilityCueLog>(Remote());
    (void)AppendAbilityCue(word, CueAt(shot, ClientNow, struck));
    (void)AppendAbilityCue(word, CueAt(shot, ClientNow, missed));
    Client.Presenter->Present(client, ClientNow);
    EXPECT_EQ(Client.Showings.Counts.size(), 2u) << "the authority's entry for the guessed target is not shown again; "
                                                     "the one the guess did not make is";
}

TEST_F(AbilityReplication, NoGuessStartsThatTheJournalCannotDecide)
{
    Grant(kShot, "asset://abilities/shot.sdata", "g_shot");
    Grant(kGlowing, "asset://abilities/glowing.sdata", "g_glow");
    Predict();
    const AbilityPredictionJournal& journal = Client.Entities.GetResource<AbilityPredictionJournal>();
    // No snapshot arrives while the client fires: every guess waits on the authority.
    const auto unheard = [&](std::vector<InputActionEdge> edges) {
        ClientTick(edges);
        Commands.emplace_back();
    };
    for (std::size_t shot = 0; shot <= AbilityPredictionJournal::kCapacity; ++shot)
    {
        unheard({ { kFire, InputEdgeKind::Press } });
        unheard({ { kFire, InputEdgeKind::Release } });
    }
    ASSERT_EQ(journal.Size(), AbilityPredictionJournal::kCapacity);

    unheard({ { kGlowAction, InputEdgeKind::Press } });
    EXPECT_EQ(Client.Glows.Held, 0) << "a guess the journal has no room for is not made";
    EXPECT_FALSE(std::ranges::any_of(RemoteRecords().Records, [](const AbilityRecord& record) {
        return record.Predicted && record.State == AbilityRecordState::Live;
    }));

    // The authority heard none of it, and refuses everything the client guessed.
    Locksteps(24);
    EXPECT_EQ(journal.Size(), 0u);
    EXPECT_EQ(Client.Glows.Held, 0);
}

TEST_F(AbilityReplication, OnlyAPredictingActorCarriesGuessedCues)
{
    Grant(kShot, "asset://abilities/shot.sdata");
    EXPECT_FALSE(Client.Entities.HasComponent<AbilityPredictedCues>(Remote()));
    EXPECT_FALSE(Authority.Entities.HasComponent<AbilityPredictedCues>(Actor));
    Predict();
    ClientTick({});
    EXPECT_TRUE(Client.Entities.HasComponent<AbilityPredictedCues>(Remote()));
}

TEST_F(AbilityReplication, GrantsSharingAKeyAreToldApartOnTheClient)
{
    ClientPeer = kOwner;
    const AbilityId ability = Authority.Define(kCharging, "asset://abilities/charging.sdata");
    const AbilityId clientAbility = Client.Define(kCharging, "asset://abilities/charging.sdata");
    // Both machines intern both entries in the same order.
    const AbilityBindingDecl low{ .Priority = 1 };
    const AbilityBindingDecl high{ .Priority = 9 };
    AbilityGrantEntries& clientEntries = Client.Entities.GetResource<AbilityGrantEntries>();
    (void)clientEntries.Intern({ "g", clientAbility, low });
    (void)clientEntries.Intern({ "g", clientAbility, high });
    AbilityGrantEntries& entries = Authority.Entities.GetResource<AbilityGrantEntries>();
    (void)entries.Intern({ "g", ability, low });
    const std::uint16_t granted = entries.Intern({ "g", ability, high });
    (void)GrantAbility(Authority.Entities, Actor, ability, AbilityGrantKind::Authored, granted);
    Step();
    (void)Publish();

    const std::span<const AbilityGrant> grants = AbilityGrantsOf(Client.Entities, Remote());
    ASSERT_EQ(grants.size(), 1u);
    const AbilityGrantEntry* entry = clientEntries.Find(grants[0].Entry);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->Override.Priority, std::optional<std::int32_t>(9))
        << "the client bound the grant to the other source's entry";
}

TEST_F(AbilityReplication, AGrantsSourceArrivesAsTheClientsOwnEntity)
{
    ClientPeer = kOwner;
    Grant(kShot, "asset://abilities/shot.sdata");
    const EntityId ring = Authority.Entities.CreateEntity();
    Authority.Entities.AddComponent<NetReplicated>(ring);
    AbilityGrantList sourced = CopyAbilityGrants(Authority.Entities, Actor);
    sourced.Entries[0].Source = MakePersistableRef(Authority.Entities, ring);
    SetAbilityGrants(Authority.Entities, Actor, sourced);
    Step();
    (void)Publish();

    const EntityId remoteRing = ClientIds.TryResolve(Identity.TryFind(ring));
    ASSERT_TRUE(remoteRing.IsValid());
    const std::span<const AbilityGrant> grants = AbilityGrantsOf(Client.Entities, Remote());
    ASSERT_EQ(grants.size(), 1u);
    EXPECT_EQ(grants[0].Source.Entity, remoteRing);
    EXPECT_FALSE(grants[0].Holder.IsValid()) << "what holds a grant stays on the authority";
}

TEST_F(AbilityReplication, GrantsThatDifferInAParameterAreToldApartOnTheClient)
{
    ClientPeer = kOwner;
    const AbilityId ability = Authority.Define(kShot, "asset://abilities/shot.sdata");
    const AbilityId clientAbility = Client.Define(kShot, "asset://abilities/shot.sdata");
    const auto entry = [](AbilityId id, double power) {
        return AbilityGrantEntry{ .Key = "g", .Ability = id, .Override = {}, .Parameters = { { "power", power } } };
    };
    AbilityGrantEntries& clientEntries = Client.Entities.GetResource<AbilityGrantEntries>();
    (void)clientEntries.Intern(entry(clientAbility, 10.0));
    (void)clientEntries.Intern(entry(clientAbility, 14.0));
    AbilityGrantEntries& entries = Authority.Entities.GetResource<AbilityGrantEntries>();
    (void)entries.Intern(entry(ability, 10.0));
    const std::uint16_t granted = entries.Intern(entry(ability, 14.0));
    (void)GrantAbility(Authority.Entities, Actor, ability, AbilityGrantKind::Authored, granted);
    Step();
    (void)Publish();

    const std::span<const AbilityGrant> grants = AbilityGrantsOf(Client.Entities, Remote());
    ASSERT_EQ(grants.size(), 1u);
    const AbilityGrantEntry* arrived = clientEntries.Find(grants[0].Entry);
    ASSERT_NE(arrived, nullptr);
    EXPECT_EQ(arrived->Parameters, (std::vector<AbilityGrantParameter>{ { "power", 14.0 } }));
}

TEST_F(AbilityReplication, ATransientGrantReachesItsOwnerAndLeavesWithItsActivation)
{
    ClientPeer = kOwner;
    constexpr std::string_view kSip = R"({ "phases": [ { "key": "p_sip",
        "then": [ { "key": "t_done", "on": "timeout", "ticks": 3, "to": "end" } ] } ] })";
    const AbilityId sip = Authority.Define(kSip, "asset://abilities/sip.sdata");
    (void)Client.Entities.GetResource<AbilityGrantEntries>().Intern({ "use", Client.Define(kSip, "asset://abilities/sip.sdata"), {} });
    const std::uint16_t entry = Authority.Entities.GetResource<AbilityGrantEntries>().Intern({ "use", sip, {} });
    Authority.Entities.GetResource<AbilityRequestQueue>().Push(
        { .Kind = AbilityRequestKind::ActivateEntry, .Actor = Actor, .Ability = sip, .Instigator = {}, .Cause = {},
          .Source = {}, .Entry = entry });
    Step();
    (void)Publish();
    const AbilityGrants* grants = Client.Entities.TryGet<AbilityGrants>(Remote());
    ASSERT_NE(grants, nullptr);
    ASSERT_EQ(grants->Count, 1u);
    EXPECT_EQ(AbilityGrantsOf(Client.Entities, Remote())[0].Kind, AbilityGrantKind::Transient);

    Run(10);
    EXPECT_EQ(Client.Entities.TryGet<AbilityGrants>(Remote())->Count, 0u);
}

// On the owner's machine each ring's grant resolves to the authority's entry and the client's own ring.
TEST_F(AbilityReplication, TwoRingsResolveToTheirOwnEntriesAndRingsOnTheOwner)
{
    ClientPeer = kOwner;
    constexpr std::string_view kFireball = R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "parameters": [ { "key": "power", "default": 0 } ],
        "phases": [ { "key": "p_cast", "then": [ { "key": "t_end", "to": "end" } ] } ] })";
    const auto declareRings = [&](Machine& machine) {
        (void)machine.Define(kFireball, "asset://abilities/fireball.sdata");
        std::vector<EffectId> rings;
        for (const auto& [path, priority, power] : { std::tuple{ "asset://effects/ring_fire.sdata", 20, 10.0 },
                                                      std::tuple{ "asset://effects/ring_ember.sdata", 21, 14.0 } })
        {
            EffectDefinition worn{};
            worn.Duration = EffectDuration::Infinite;
            const EffectId effect = machine.Entities.GetResource<EffectRegistry>().Define(path, worn);
            DeclareEffectGrants(machine.Entities, effect,
                                { AbilityGrantDeclaration{ .Key = "g_fireball", .Ability = "asset://abilities/fireball.sdata",
                                                           .Override = { .Priority = priority },
                                                           .Parameters = { { "power", power } } } });
            rings.push_back(effect);
        }
        return rings;
    };
    (void)declareRings(Client);
    const std::vector<EffectId> rings = declareRings(Authority);
    std::vector<EntityId> worn;
    for (const EffectId ring : rings)
    {
        const EntityId item = Authority.Entities.CreateEntity();
        Authority.Entities.AddComponent<NetReplicated>(item);
        (void)ApplyEffect(Authority.Entities, Actor, ring, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now },
                          LeaseOwner{ .Entity = item });
        worn.push_back(item);
    }
    Step();
    (void)Publish();

    const std::span<const AbilityGrant> grants = AbilityGrantsOf(Client.Entities, Remote());
    ASSERT_EQ(grants.size(), 2u);
    const AbilityGrantEntries& entries = Client.Entities.GetResource<AbilityGrantEntries>();
    for (std::size_t i = 0; i < worn.size(); ++i)
    {
        const EntityId mirror = ClientIds.TryResolve(Identity.TryFind(worn[i]));
        ASSERT_TRUE(mirror.IsValid());
        const auto grant = std::find_if(grants.begin(), grants.end(),
                                        [&](const AbilityGrant& held) { return held.Source.Entity == mirror; });
        ASSERT_NE(grant, grants.end()) << "each ring is its own grant's source";
        const AbilityGrantEntry* entry = entries.Find(grant->Entry);
        ASSERT_NE(entry, nullptr) << "bound, not held unbound";
        EXPECT_EQ(entry->Parameters, (std::vector<AbilityGrantParameter>{ { "power", i == 0 ? 10.0 : 14.0 } }));
    }
}

TEST_F(AbilityReplication, AGuessedBowShotChecksBothCostsAndPaysNeither)
{
    constexpr std::string_view kBowShot = R"({
        "input_default": { "action": "Input.PrimaryFire" },
        "costs": [ { "key": "g_shot",
                     "effects": [ { "data": "asset://effects/energy_10.sdata" } ],
                     "resources": [ { "key": "r_arrow", "provider": "test.counter",
                                      "arguments": { "holder": { "port": "actor" } } } ] } ],
        "phases": [ { "key": "p_draw",
            "enter": [ { "key": "o_pay", "op": "commit", "groups": ["g_shot"] },
                       { "key": "o_show", "op": "invoke", "verb": "test.show" } ],
            "then": [ { "key": "t_end", "to": "end" } ] } ] })";
    for (Machine* machine : { &Authority, &Client })
    {
        ASSERT_TRUE(DeclareCounterCostProvider(machine->Entities));
        const AttributeId energy = machine->Entities.GetResource<AttributeRegistry>().FindAttribute("Energy");
        EffectDefinition stamina{};
        stamina.Modifiers = { { energy, ModifierOp::Add, -10.0f } };
        machine->Entities.GetResource<EffectRegistry>().Define("asset://effects/energy_10.sdata", stamina);
    }
    const AttributeId energy = Authority.Entities.GetResource<AttributeRegistry>().FindAttribute("Energy");
    (void)Authority.Entities.TryGet<AttributeSet>(Actor)->Add(energy, 100.0f);
    Grant(kBowShot, "asset://abilities/bow_shot.sdata");
    Authority.Entities.GetResource<Counters>().Held[Actor] = 1;
    Predict();
    Client.Entities.GetResource<Counters>().Held[Remote()] = 1;

    Lockstep({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(Client.Showings.Total(), 1) << "the guess committed";
    EXPECT_GE(Client.Entities.GetResource<Counters>().Checks, 1u);
    EXPECT_EQ(Client.Entities.GetResource<Counters>().Held[Remote()], 1) << "a guess pays no arrow";
    const AttributeId clientEnergy = Client.Entities.GetResource<AttributeRegistry>().FindAttribute("Energy");
    EXPECT_FLOAT_EQ(Client.Entities.TryGet<AttributeSet>(Remote())->GetBase(clientEnergy), 100.0f)
        << "and no stamina";

    Lockstep({ { kFire, InputEdgeKind::Release } });
    Locksteps(10);
    EXPECT_EQ(Authority.Entities.GetResource<Counters>().Held[Actor], 0);
    EXPECT_FLOAT_EQ(Authority.Entities.TryGet<AttributeSet>(Actor)->GetBase(energy), 90.0f);
    EXPECT_FLOAT_EQ(Client.Entities.TryGet<AttributeSet>(Remote())->GetBase(clientEnergy), 90.0f)
        << "the authority's payment arrives";
}

// An equipped item's grant is worn as its slot binds it, on every machine: the
// owner resolves the worn entry the authority granted, and predicts the press.
TEST_F(AbilityReplication, AnEquippedGrantIsBoundByItsSlotOnTheOwnerWhoPredictsIt)
{
    ClientPeer = kOwner;
    const auto declare = [&](Machine& machine) {
        World& world = machine.Entities;
        InstallInventoryVocabulary(world);
        InstallEquipmentVocabulary(world);
        const GameplayTagId ring = *world.GetResource<GameplayTagRegistry>().RegisterTag("Item.Ring");
        (void)machine.Define(Levelled("simulation"), "asset://abilities/warded.sdata");
        EffectDefinition worn{};
        worn.Duration = EffectDuration::Infinite;
        const EffectId effect = world.GetResource<EffectRegistry>().Define("asset://effects/ward.sdata", worn);
        (void)DeclareEffectGrants(world, effect,
                                  { AbilityGrantDeclaration{ .Key = "g_ward", .Ability = "asset://abilities/warded.sdata",
                                                             .Override = {} } });
        EquipmentLayout layout;
        EquipmentSlot left;
        left.Key = "ring.left";
        left.Accepts = ring;
        left.Binding.Action = "Input.PrimaryFire";
        left.Binding.Priority = 20;
        layout.Slots.push_back(std::move(left));
        const EquipmentLayoutId body =
            world.GetResource<EquipmentLayoutRegistry>().Define("asset://equipment/body.sdata", std::move(layout));
        const ItemId item = world.GetResource<ItemRegistry>().Define(
            "asset://items/ward_ring.sdata",
            ItemDefinition{ .Name = "Ward Ring", .Tags = { ring }, .Occupies = { "ring" },
                            .EquipEffects = { { effect, 1.0f } } });
        std::vector<std::string> errors;
        DeclareWornGrantEntries(world, errors);
        EXPECT_TRUE(errors.empty()) << errors.front();
        return std::pair{ body, item };
    };
    (void)declare(Client);
    const auto [body, item] = declare(Authority);
    World& world = Authority.Entities;
    world.AddComponent<ItemContainer>(Actor, ItemContainer{});
    world.AddComponent<EquipmentState>(Actor, EquipmentState{ .Layout = body, .Count = 0, .NextKey = 1, .Records = {} });
    const EntityId ring = Inventory(world, FixedSimTime{}).Give(Actor, item, 1).Entry;
    ASSERT_EQ(Equipment(world, FixedSimTime{ .DeltaSeconds = kTickSeconds, .TickIndex = Now }).Equip(Actor, ring).Refusal,
              EquipmentRefusal::None);
    Step();
    (void)Publish();

    const std::span<const AbilityGrant> grants = AbilityGrantsOf(Client.Entities, Remote());
    ASSERT_EQ(grants.size(), 1u);
    const AbilityGrantEntry* entry = Client.Entities.GetResource<AbilityGrantEntries>().Find(grants[0].Entry);
    ASSERT_NE(entry, nullptr) << "the worn entry was declared on the owner too";
    EXPECT_EQ(entry->Override.Priority, 20) << "bound by its slot";

    Predict();
    Lockstep({ { kFire, InputEdgeKind::Press } });
    EXPECT_EQ(Client.Simulated.Held, 1) << "the owner guesses the press ahead of the authority";
}

// An entity is written whole or not at all, so an owner seed over one datagram never arrives.
TEST_F(AbilityReplication, AShippingPawnsOwnerSeedFitsOneSnapshot)
{
    ClientPeer = kOwner;
    ShippingPawn();
    (void)Publish();
    EXPECT_EQ(LastWrite.EntitiesUnsendable, 0u);
    EXPECT_EQ(LastWrite.EntitiesWritten, 1u);
    RecordProperty("seed_bytes", std::to_string(LastWrite.BytesWritten));
}

// What is worn crosses as one run per record: a change resends that record, an
// owner is told each record's source, and another peer is told what is worn.
#define SENCHA_WORN_RECORDS_TEST(Peer, PeerId)                                                            \
    TEST_F(AbilityReplication, WornRecordsReachThe##Peer)                                                 \
    {                                                                                                     \
        ClientPeer = PeerId;                                                                              \
        const bool owner = PeerId == kOwner;                                                              \
        const std::vector<EntityId> entries = Wear(3, true);                                              \
        (void)Publish();                                                                                  \
        for (std::uint32_t key = 1; key <= 3; ++key)                                                      \
        {                                                                                                 \
            const EquipmentRecord* record = ClientRecord(key);                                            \
            ASSERT_NE(record, nullptr);                                                                   \
            EXPECT_EQ(record->Source.Entity, owner ? Mirror(entries[key - 1]) : EntityId{});              \
        }                                                                                                 \
        /* The first record ends; the last takes its place. */                                            \
        EndEquipmentRecord(Authority.Entities, Actor, 0);                                                 \
        RecordProperty("unequip_snapshot_bytes", std::to_string(Publish()));                              \
        EXPECT_EQ(ClientRecord(1), nullptr);                                                              \
        for (std::uint32_t key = 2; key <= 3; ++key)                                                      \
        {                                                                                                 \
            const EquipmentRecord* record = ClientRecord(key);                                            \
            ASSERT_NE(record, nullptr) << "record " << key;                                               \
            EXPECT_EQ(record->Source.Entity, owner ? Mirror(entries[key - 1]) : EntityId{});              \
        }                                                                                                 \
    }
SENCHA_WORN_RECORDS_TEST(Owner, kOwner)
SENCHA_WORN_RECORDS_TEST(Observer, 2u)
#undef SENCHA_WORN_RECORDS_TEST

// Thirty records, each a run of its own, in one snapshot.
#define SENCHA_MANY_WORN_RECORDS_TEST(Peer, PeerId)                                                       \
    TEST_F(AbilityReplication, ThirtyWornRecordsReachThe##Peer)                                           \
    {                                                                                                     \
        ClientPeer = PeerId;                                                                              \
        Budget = kNetDefaultSnapshotBytes;                                                                \
        const bool owner = PeerId == kOwner;                                                              \
        const std::vector<EntityId> entries = Wear(30, true);                                             \
        (void)Publish();                                                                                  \
        EXPECT_EQ(LastWrite.EntitiesUnsendable, 0u);                                                      \
        ASSERT_EQ(EquipmentRecordsOf(Client.Entities, Remote()).size(), 30u);                             \
        for (std::uint32_t key = 1; key <= 30; ++key)                                                     \
        {                                                                                                 \
            const EquipmentRecord* record = ClientRecord(key);                                            \
            ASSERT_NE(record, nullptr) << "record " << key;                                               \
            EXPECT_EQ(record->Slots, 1u << (key - 1));                                                    \
            EXPECT_EQ(record->Primary, key - 1);                                                          \
            EXPECT_EQ(record->Source.Entity, owner ? Mirror(entries[key - 1]) : EntityId{});              \
        }                                                                                                 \
        EndEquipmentRecord(Authority.Entities, Actor, 4);                                                 \
        (void)Publish();                                                                                  \
        EXPECT_EQ(EquipmentRecordsOf(Client.Entities, Remote()).size(), 29u);                             \
        EXPECT_EQ(ClientRecord(5), nullptr);                                                              \
        ASSERT_NE(ClientRecord(30), nullptr);                                                             \
        EXPECT_EQ(ClientRecord(30)->Source.Entity, owner ? Mirror(entries[29]) : EntityId{});             \
    }
SENCHA_MANY_WORN_RECORDS_TEST(Owner, kOwner)
SENCHA_MANY_WORN_RECORDS_TEST(Observer, 2u)
#undef SENCHA_MANY_WORN_RECORDS_TEST

// Every grant an actor can hold reaches its owner with the authority's ids, in
// grant order; a revoke in the middle keeps the rest as they were.
TEST_F(AbilityReplication, AFullGrantListReachesItsOwnerInOrder)
{
    ClientPeer = kOwner;
    Budget = kNetDefaultSnapshotBytes;
    for (std::size_t i = 0; i < kAbilityGrantCapacity; ++i)
        Grant(kShot, "asset://abilities/shot_" + std::to_string(i) + ".sdata", "g_" + std::to_string(i));
    (void)Publish();
    EXPECT_EQ(LastWrite.EntitiesUnsendable, 0u);
    const auto matches = [&] {
        const std::span<const AbilityGrant> sent = AbilityGrantsOf(Authority.Entities, Actor);
        const std::span<const AbilityGrant> held = AbilityGrantsOf(Client.Entities, Remote());
        ASSERT_EQ(held.size(), sent.size());
        const AbilityRegistry& authority = Authority.Entities.GetResource<AbilityRegistry>();
        const AbilityRegistry& client = Client.Entities.GetResource<AbilityRegistry>();
        for (std::size_t i = 0; i < sent.size(); ++i)
        {
            EXPECT_EQ(held[i].Id, sent[i].Id) << i;
            EXPECT_EQ(client.GetName(held[i].Ability), authority.GetName(sent[i].Ability)) << i;
            EXPECT_NE(held[i].Entry, kAbilityNoGrantEntry) << i;
        }
        EXPECT_EQ(Client.Entities.TryGet<AbilityGrants>(Remote())->NextId,
                  Authority.Entities.TryGet<AbilityGrants>(Actor)->NextId);
    };
    ASSERT_EQ(AbilityGrantsOf(Client.Entities, Remote()).size(), kAbilityGrantCapacity);
    matches();

    ASSERT_TRUE(RevokeAbilityGrant(Authority.Entities, Actor, AbilityGrantsOf(Authority.Entities, Actor)[21].Id));
    (void)Publish();
    ASSERT_EQ(AbilityGrantsOf(Client.Entities, Remote()).size(), kAbilityGrantCapacity - 1);
    matches();
}

TEST_F(AbilityReplication, AnotherPeerIsSentNoneOfAFullGrantList)
{
    ClientPeer = 2;
    Budget = kNetDefaultSnapshotBytes;
    for (std::size_t i = 0; i < kAbilityGrantCapacity; ++i)
        Grant(kShot, "asset://abilities/shot_" + std::to_string(i) + ".sdata", "g_" + std::to_string(i));
    const std::size_t bare = Publish();
    EXPECT_TRUE(AbilityGrantsOf(Client.Entities, Remote()).empty()) << "grants are the owner's";
    RecordProperty("observer_seed_bytes", std::to_string(bare));
}

TEST_F(AbilityReplication, AnActorsGrantImageIsSizedByWhatItHolds)
{
    ClientPeer = kOwner;
    for (int i = 0; i < 3; ++i)
        Grant(kShot, "asset://abilities/shot_" + std::to_string(i) + ".sdata", "g_" + std::to_string(i));
    (void)Publish();
    ASSERT_EQ(AbilityGrantsOf(Client.Entities, Remote()).size(), 3u);
    const ReplicatedComponent* grants = Layout.Find(ResolveComponentTypeId<AbilityGrants>());
    const ReplicationChangeStore::EntityState* held = Changes.Find(Identity.TryFind(Actor));
    ASSERT_NE(held, nullptr);
    const auto image = std::ranges::find_if(held->Components, [&](const ReplicationChangeStore::ComponentState& state) {
        return Layout.At(state.WireIndex) == grants;
    });
    ASSERT_NE(image, held->Components.end());
    EXPECT_LT(image->Bytes.size(), 100u) << "three grants are an image of three, not of the most an actor may hold";
}

TEST_F(AbilityReplication, AWearersImageIsSizedByWhatItWears)
{
    ClientPeer = kOwner;
    (void)Wear(4, true);
    (void)Publish();
    ASSERT_EQ(EquipmentRecordsOf(Client.Entities, Remote()).size(), 4u);
    const ReplicatedComponent* equipment = Layout.Find(ResolveComponentTypeId<EquipmentState>());
    const ReplicationChangeStore::EntityState* held = Changes.Find(Identity.TryFind(Actor));
    ASSERT_NE(held, nullptr);
    const auto image = std::ranges::find_if(held->Components, [&](const ReplicationChangeStore::ComponentState& state) {
        return Layout.At(state.WireIndex) == equipment;
    });
    ASSERT_NE(image, held->Components.end());
    EXPECT_LT(image->Bytes.size(), 140u) << "four records are an image of four, not of every slot a layout may have";
}

// A client that heard nothing while records came and went is sent what is worn now.
TEST_F(AbilityReplication, ALateOwnerIsSentWhatIsWornNow)
{
    ClientPeer = kOwner;
    const std::vector<EntityId> entries = Wear(3, true);
    (void)Publish(false);
    EndEquipmentRecord(Authority.Entities, Actor, 1);
    (void)Publish(false);
    (void)Publish();
    EXPECT_EQ(ClientRecord(2), nullptr);
    for (const std::uint32_t key : { 1u, 3u })
    {
        const EquipmentRecord* record = ClientRecord(key);
        ASSERT_NE(record, nullptr) << "record " << key;
        EXPECT_EQ(record->Source.Entity, Mirror(entries[key - 1]));
    }
}

// A record whose source the owner has not been sent (it lies in a chest no
// peer is sent) binds the source when it arrives.
TEST_F(AbilityReplication, ARecordsSourceArrivingLaterBindsOnTheOwner)
{
    ClientPeer = kOwner;
    (void)Wear(0, true);
    World& world = Authority.Entities;
    const EntityId chest = world.CreateEntity();
    world.AddComponent<ItemContainer>(chest, ItemContainer{});
    const ItemId charm = world.GetResource<ItemRegistry>().Find("asset://items/charm.sdata");
    const EntityId entry = Inventory(world, FixedSimTime{}).Give(chest, charm, 1).Entry;
    EquipmentRecordList worn = CopyEquipment(world, Actor);
    worn.Records = { EquipmentRecord{ .Key = worn.NextKey++, .Definition = charm, .Slots = 1u, .Primary = 0,
                                      .Source = MakePersistableRef(world, entry),
                                      .Derivation = EquipmentDerivation::Pending } };
    SetEquipment(world, Actor, worn);
    (void)Publish();
    const EquipmentRecord* waiting = ClientRecord(1);
    ASSERT_NE(waiting, nullptr);
    EXPECT_FALSE(waiting->Source.Entity.IsValid()) << "not sent the entry yet";
    ASSERT_EQ(Inventory(world, FixedSimTime{}).Transfer(entry, Actor, 0).Refusal, InventoryRefusal::None);
    (void)Publish();
    (void)Publish();
    const EquipmentRecord* bound = ClientRecord(1);
    ASSERT_NE(bound, nullptr);
    EXPECT_TRUE(Mirror(entry).IsValid());
    EXPECT_EQ(bound->Source.Entity, Mirror(entry));
}

// D5 (docs/gameplay/equipment.md, "Wire"): the shipping pawn wearing its
// largest legal EquipmentState is sent to its owner and to everyone else, and
// the difference with and without the component is what equipment costs.
#define SENCHA_EQUIPMENT_SEED_TEST(Records, Worn, Peer)                                                   \
    TEST_F(AbilityReplication, AShippingPawnsSeedWithEquipment_##Records##_##Worn##_##Peer)               \
    {                                                                                                     \
        ClientPeer = std::string_view(#Peer) == "owner" ? kOwner : 2u;                                    \
        ShippingPawn();                                                                                   \
        (void)Wear(Records, std::string_view(#Worn) == "worn");                                           \
        (void)Publish();                                                                                  \
        EXPECT_EQ(LastWrite.EntitiesUnsendable, 0u);                                                      \
        RecordProperty("snapshot_bytes", std::to_string(LastWrite.BytesWritten));                        \
        std::cout << "equipment seed: " << Records << " records, " << #Worn << ", " << #Peer << ": "      \
                  << LastWrite.BytesWritten << " bytes, " << LastWrite.EntitiesWritten << " entities\n"; \
    }
SENCHA_EQUIPMENT_SEED_TEST(0, bare, owner)
SENCHA_EQUIPMENT_SEED_TEST(0, worn, owner)
SENCHA_EQUIPMENT_SEED_TEST(4, bare, owner)
SENCHA_EQUIPMENT_SEED_TEST(4, worn, owner)
SENCHA_EQUIPMENT_SEED_TEST(16, bare, owner)
SENCHA_EQUIPMENT_SEED_TEST(16, worn, owner)
SENCHA_EQUIPMENT_SEED_TEST(32, bare, owner)
SENCHA_EQUIPMENT_SEED_TEST(32, worn, owner)
SENCHA_EQUIPMENT_SEED_TEST(0, bare, observer)
SENCHA_EQUIPMENT_SEED_TEST(0, worn, observer)
SENCHA_EQUIPMENT_SEED_TEST(4, bare, observer)
SENCHA_EQUIPMENT_SEED_TEST(4, worn, observer)
SENCHA_EQUIPMENT_SEED_TEST(16, bare, observer)
SENCHA_EQUIPMENT_SEED_TEST(16, worn, observer)
SENCHA_EQUIPMENT_SEED_TEST(32, bare, observer)
SENCHA_EQUIPMENT_SEED_TEST(32, worn, observer)
#undef SENCHA_EQUIPMENT_SEED_TEST

std::vector<std::uint64_t> AbilityReplication::PublishOwedElements(ComponentTypeId type)
{
    const std::vector<std::byte> bytes = Write();
    const NetEntityId id = Identity.TryFind(Actor);
    const ReplicationChangeStore::EntityState* entity = Changes.Find(id);
    const std::uint64_t floor = Peer.Floor(id);
    std::vector<std::uint64_t> elements;
    for (const ReplicationChangeStore::ComponentState& held : entity->Components)
        if (Layout.At(held.WireIndex)->Type == type)
            (void)ReplicationOwedFields(*Layout.At(held.WireIndex), held, floor, entity->OwnerChangedAt > floor,
                                        entity->Owner == ClientPeer, &elements);
    Apply(bytes);
    return elements;
}

TEST_F(AbilityReplication, ATagChangeSendsOneElement)
{
    ClientPeer = kOwner;
    ShippingPawn();
    Authority.Entities.RemoveComponent<NetSpawnPrefab>(Actor);
    (void)Publish();
    (void)Publish();
    ASSERT_TRUE(Remote().IsValid());
    const GameplayTagId charged = Authority.Entities.GetResource<GameplayTagRegistry>().FindTag("State.Charged");
    (void)Authority.Entities.TryGet<GameplayTagContainer>(Actor)->Grant(charged);
    const std::vector<std::uint64_t> owed = PublishOwedElements(ResolveComponentTypeId<GameplayTagContainer>());
    EXPECT_EQ(std::ranges::count_if(owed, [](std::uint64_t runs) { return runs != 0; }), 1)
        << "a tag gaining a stack resends that tag alone";
    EXPECT_NE(std::ranges::find(owed, std::uint64_t{ 0b10 }), owed.end()) << "and of it, its count alone";
    EXPECT_EQ(Client.Entities.TryGet<GameplayTagContainer>(Remote())->StackCount(
                  Client.Entities.GetResource<GameplayTagRegistry>().FindTag("State.Charged")),
              2u);
}

TEST_F(AbilityReplication, AnAttributeChangeSendsOneValue)
{
    ClientPeer = kOwner;
    ShippingPawn();
    Authority.Entities.RemoveComponent<NetSpawnPrefab>(Actor);
    (void)Publish();
    (void)Publish();
    ASSERT_TRUE(Remote().IsValid());
    const AttributeId health = Authority.Entities.GetResource<AttributeRegistry>().FindAttribute("Health");
    *Authority.Entities.TryGet<AttributeSet>(Actor)->CurrentPtr(health) = 42.0f;
    const std::vector<std::uint64_t> owed = PublishOwedElements(ResolveComponentTypeId<AttributeSet>());
    EXPECT_EQ(std::ranges::count_if(owed, [](std::uint64_t runs) { return runs != 0; }), 1)
        << "one attribute's value resends that attribute alone";
    EXPECT_NE(std::ranges::find(owed, std::uint64_t{ 0b100 }), owed.end()) << "and of it, its current value alone";
    const AttributeSet* held = Client.Entities.TryGet<AttributeSet>(Remote());
    EXPECT_FLOAT_EQ(held->GetCurrent(Client.Entities.GetResource<AttributeRegistry>().FindAttribute("Health")), 42.0f);
}

// Measured, not asserted; scripts/bench_replication.sh records it.
void AbilityReplication::MeasureFullPawnsOwnerSeed(std::size_t grants)
{
    ClientPeer = kOwner;
    Budget = kNetDefaultSnapshotBytes;
    Grant(kAutomatic, "asset://abilities/automatic.sdata", "g_fire");
    for (std::size_t i = 1; i < grants; ++i)
        Grant(kGlowing, "asset://abilities/glow_" + std::to_string(i) + ".sdata", "g_" + std::to_string(i));
    AttributeSet& attributes = *Authority.Entities.TryGet<AttributeSet>(Actor);
    for (std::size_t i = 0; i < AttributeSet::Capacity; ++i)
    {
        const std::string name = "Bench.Attribute." + std::to_string(i);
        (void)Client.Entities.GetResource<AttributeRegistry>().RegisterAttribute(name);
        (void)attributes.Add(Authority.Entities.GetResource<AttributeRegistry>().RegisterAttribute(name), 7.5f + static_cast<float>(i));
    }
    World& world = Authority.Entities;
    world.AddComponent<CharacterMovement>(Actor, CharacterMovement{ .Mode = LocomotionModeId{ 2 } });
    world.TryGet<KinematicState>(Actor)->Velocity = Vec3d(3.0f, -1.0f, 2.0f);
    world.TryGet<JumpState>(Actor)->CooldownRemaining = 0.25f;
    world.AddComponent<NetSpawnPrefab>(Actor, NetSpawnPrefab{ .Scene = AssetId{ 0xC3DD45711F65F6EBull } });
    world.AddComponent<NetDrivenBy>(Actor, NetDrivenBy{ .Peer = kOwner });
    Step({ { kFire, InputEdgeKind::Press } });
    Steps(3);
    GameplayTagContainer& tags = *world.TryGet<GameplayTagContainer>(Actor);
    for (std::size_t i = tags.Count; i < GameplayTagContainer::Capacity; ++i)
    {
        const std::string name = "Bench.Tag." + std::to_string(i);
        (void)Client.Entities.GetResource<GameplayTagRegistry>().RegisterTag(name);
        (void)tags.Grant(*world.GetResource<GameplayTagRegistry>().RegisterTag(name));
    }

    (void)Publish();
    std::cout << "full pawn owner seed, " << grants << " grants: " << LastWrite.BytesWritten << " bytes of "
              << kNetDefaultSnapshotBytes << ", written " << LastWrite.EntitiesWritten << ", unsendable "
              << LastWrite.EntitiesUnsendable << "\n";
}

#define SENCHA_FULL_PAWN_SEED_TEST(Name, Grants)                                                          \
    TEST_F(AbilityReplication, Name)                                                                      \
    {                                                                                                     \
        if (std::getenv("SENCHA_REPLICATION_BENCH_OUT") == nullptr)                                       \
            GTEST_SKIP() << "set SENCHA_REPLICATION_BENCH_OUT to measure (use scripts/bench_replication.sh)"; \
        MeasureFullPawnsOwnerSeed(Grants);                                                                \
    }
SENCHA_FULL_PAWN_SEED_TEST(AFullPawnsOwnerSeedIsMeasured, kAbilityGrantCapacity)
SENCHA_FULL_PAWN_SEED_TEST(AFullPawnsOwnerSeedIsMeasured_16Grants, 16)
#undef SENCHA_FULL_PAWN_SEED_TEST

AbilityReplication::Delta AbilityReplication::PublishMeasured()
{
    const std::vector<std::byte> bytes = Write();
    const NetEntityId id = Identity.TryFind(Actor);
    const ReplicationChangeStore::EntityState* entity = Changes.Find(id);
    const std::uint64_t floor = Peer.Floor(id);
    Delta delta;
    delta.Bits = LastWrite.DeltaBits;
    for (const ReplicationChangeStore::ComponentState& held : entity->Components)
    {
        const ReplicatedComponent& component = *Layout.At(held.WireIndex);
        ++delta.Components;
        if (ReplicationOwedFields(component, held, floor, entity->OwnerChangedAt > floor, entity->Owner == ClientPeer)
            != 0)
            continue;
        ++delta.Empty;
        const std::size_t maskBits = component.Fields.size() + component.Lists.size();
        delta.EmptyBits += ReplicationSnapshotWire::ComponentIndexBits + maskBits;
        delta.EmptyNames += std::string(delta.EmptyNames.empty() ? "" : " ") + std::string(component.Name) + "("
                          + std::to_string(maskBits) + ")";
    }
    Apply(bytes);
    Client.Presenter->Present(Client.Entities, Now);
    return delta;
}

void AbilityReplication::MeasureMovingActorsDelta(bool wearing)
{
    ClientPeer = 2;
    ShippingPawn();
    if (wearing)
        (void)Wear(4, true);
    (void)Publish();
    (void)Publish();
    Delta total;
    std::size_t publishes = 0;
    for (int i = 0; i < 40; ++i)
    {
        Authority.Entities.TryGet<LocalTransform>(Actor)->Value.Position.X += 0.25f;
        Step();
        if (Now % kNetDefaultSnapshotInterval != 0)
            continue;
        const Delta delta = PublishMeasured();
        total.Bits += delta.Bits;
        total.Components = delta.Components;
        total.Empty += delta.Empty;
        total.EmptyBits += delta.EmptyBits;
        total.EmptyNames = delta.EmptyNames;
        ++publishes;
    }
    ASSERT_GT(publishes, 0u);
    const double count = static_cast<double>(publishes);
    std::cout << "moving actor delta to an observer, " << (wearing ? "wearing 4 records" : "bare") << ": "
              << static_cast<double>(total.Bits) / 8.0 / count << " bytes a snapshot over " << publishes
              << " snapshots; " << total.Components << " components carried, "
              << static_cast<double>(total.Empty) / count << " owed nothing, "
              << static_cast<double>(total.EmptyBits) / 8.0 / count << " bytes of index and mask for them ["
              << total.EmptyNames << "]\n";
}

void AbilityReplication::MeasureOwnerChange(std::string_view what, const std::function<void()>& change)
{
    ClientPeer = kOwner;
    ShippingPawn();
    (void)Publish();
    (void)Publish();
    (void)Publish();
    ASSERT_EQ(LastWrite.EntitiesWritten, 0u) << "the pawn must be still before the change is measured";
    change();
    const Delta delta = PublishMeasured();
    std::cout << what << " to the owner: " << static_cast<double>(delta.Bits) / 8.0 << " bytes; "
              << delta.Components << " components carried, " << delta.Empty << " owed nothing, "
              << static_cast<double>(delta.EmptyBits) / 8.0 << " bytes of index and mask for them ["
              << delta.EmptyNames << "]\n";
}

#define SENCHA_SKIP_UNLESS_MEASURING()                                                                       \
    if (std::getenv("SENCHA_REPLICATION_BENCH_OUT") == nullptr)                                               \
    GTEST_SKIP() << "set SENCHA_REPLICATION_BENCH_OUT to measure (use scripts/bench_replication.sh)"

TEST_F(AbilityReplication, AMovingActorsDeltaToAnObserverIsMeasured)
{
    SENCHA_SKIP_UNLESS_MEASURING();
    MeasureMovingActorsDelta(false);
}

TEST_F(AbilityReplication, AMovingWearersDeltaToAnObserverIsMeasured)
{
    SENCHA_SKIP_UNLESS_MEASURING();
    MeasureMovingActorsDelta(true);
}

TEST_F(AbilityReplication, ATagChangeToTheOwnerIsMeasured)
{
    SENCHA_SKIP_UNLESS_MEASURING();
    MeasureOwnerChange("a tag gaining a stack", [this] {
        const GameplayTagId charged = Authority.Entities.GetResource<GameplayTagRegistry>().FindTag("State.Charged");
        (void)Authority.Entities.TryGet<GameplayTagContainer>(Actor)->Grant(charged);
    });
}

TEST_F(AbilityReplication, AnAttributeChangeToTheOwnerIsMeasured)
{
    SENCHA_SKIP_UNLESS_MEASURING();
    MeasureOwnerChange("one attribute's current value", [this] {
        const AttributeId health = Authority.Entities.GetResource<AttributeRegistry>().FindAttribute("Health");
        *Authority.Entities.TryGet<AttributeSet>(Actor)->CurrentPtr(health) = 42.0f;
    });
}
#undef SENCHA_SKIP_UNLESS_MEASURING
