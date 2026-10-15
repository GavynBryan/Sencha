// Abilities seen from another machine: the authority's records and cue log
// travel as bytes into a client whose tag ids, ability ids and entity ids all
// differ, and the client presents them -- through loss, in order, once each,
// and within the loss bounds docs/gameplay/abilitykit.md states.

#include <abilities/AbilityDefinitionData.h>
#include <abilities/AbilityDefinitionResolve.h>
#include <abilities/AbilityKit.h>
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
#include <net/NetReplicationComponents.h>
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
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>
#include <numeric>
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
            EXPECT_TRUE(verbs.Commit());
        }
        EXPECT_TRUE(DeclareEngineVocabulary(Entities));
        Actions.Rebuild(std::vector<InputActionDefinition>{ { .Name = "Input.PrimaryFire" }, { .Name = "Input.Glow" } });
        Entities.SetResource(InputActionVocabulary{ &Actions, 1 });
        Entities.AddResource<InputActionState>().Configure(2);

        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Queries = std::make_unique<AuthoredQueryDispatcher>(*FindAuthoredQueryRegistry(Entities));
        Bindings = std::make_unique<EngineVocabularyBindings>(Entities, Clock, *Verbs);
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.show"), Showings));
        Tokens.push_back(Verbs->Bind(Verbs->Registry().Find("test.glow"), Glows));
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

    void Grant(std::string_view json, std::string_view path)
    {
        const AbilityId ability = Authority.Define(json, path);
        // The client's own copy of the grant entry, as its prefab interns one.
        (void)Client.Entities.GetResource<AbilityGrantEntries>().Intern({ "g", Client.Define(json, path), {} });
        const std::uint16_t entry = Authority.Entities.GetResource<AbilityGrantEntries>().Intern({ "g", ability, {} });
        (void)GrantAbility(*Authority.Entities.TryGet<AbilityGrants>(Actor), ability, AbilityGrantKind::Authored, entry);
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
        const SnapshotWriteResult written = ReplicationWriteSnapshot(write, Scratch);
        EXPECT_TRUE(written.Ok);
        return { Scratch.begin(), Scratch.begin() + static_cast<std::ptrdiff_t>(written.BytesWritten) };
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
        return static_cast<const World&>(Authority.Entities).TryGet<ActiveAbilities>(Actor)->Records[0];
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

    [[nodiscard]] const ActiveAbilities& RemoteRecords() const
    {
        return *static_cast<const World&>(Client.Entities).TryGet<ActiveAbilities>(Remote());
    }

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
    const AbilityRecord& record = RemoteRecords().Records[0];
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
    const std::size_t perSecond = firing - moving;
    RecordProperty("cue_bytes_per_second", static_cast<int>(perSecond));
    std::cout << "ability bytes per second for one firing actor, to one peer: " << perSecond << "\n";
    EXPECT_LE(perSecond, 360u) << "the presentation budget per firing actor per peer";
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
    const AbilityGrants& grants = *client.TryGet<AbilityGrants>(Remote());
    ASSERT_EQ(grants.Count, 1u);
    EXPECT_EQ(grants.Entries[0].Ability, (*client.TryGetResource<AbilityRegistry>()).Find("asset://abilities/charging.sdata"));
    EXPECT_NE(grants.Entries[0].Entry, kAbilityNoGrantEntry) << "bound to the client's own entry for the same grant";
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
