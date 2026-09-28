// An authority and a client with separate Worlds, tag numbering and tick counts,
// joined only by snapshot bytes. Only the request set replicates, so the client
// must derive the same sections, loops, times and tails from it alone.

#include "AnimFlowFixture.h"

#include <anim/AnimEventSystem.h>
#include <anim/AnimRequestJournal.h>
#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <ecs/WorldComponentSchema.h>
#include <net/NetDesyncProbe.h>
#include <net/NetSnapshotAck.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>
#include <world/SimulationAuthority.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace
{
    // Two machines running the same content. The client registers tags the
    // authority does not, ahead of the content's own, so every tag id differs
    // between them, and counts its ticks from a number of its own.
    struct AnimSession
    {
        using Content = std::function<DataAssetHandle(AnimRigFixture&)>;

        WorldComponentSchema Schema;
        ReplicationLayout Layout;
        AnimRigFixture Authority;
        AnimRigFixture Client{ { "test.client.first", "test.client.second", "test.client.third" } };
        DataAssetHandle AuthorityRig;
        DataAssetHandle ClientRig;
        EntityId Prop;
        EntityId Mirror;

        ReplicationAuthorityIdentity Identity;
        ReplicationChangeStore Changes;
        ReplicationPeerState Peer;
        ReplicationClientIdentity ClientIds;
        NetSnapshotAck Ack;
        std::uint64_t Generation = 0;
        std::vector<std::byte> Scratch = std::vector<std::byte>(64 * 1024);
        // The client's own tick counter, unrelated to the authority's.
        std::uint64_t ClientLocalTick = 5000;
        // The last of the client's commands the authority has processed.
        std::uint64_t CommandAck = 0;

        // `clientContent`, when given, is what the client loads instead: a
        // client whose build or data differs from the authority's.
        explicit AnimSession(const Content& content, const Content& clientContent = {})
        {
            ComponentRegistrar components(&Schema, nullptr, &Layout);
            components.Add<NetReplicated>();
            RegisterAnimationComponents(components);
            Schema.Seal();
            Layout.Seal();

            AuthorityRig = content(Authority);
            ClientRig = clientContent ? clientContent(Client) : content(Client);
            // Entities the client made for itself first, so the same entity
            // has a different id on each machine.
            for (int i = 0; i < 3; ++i)
                (void)Client.Entities.CreateEntity();
            Prop = Authority.Character(AuthorityRig);
            Authority.Entities.AddComponent<NetReplicated>(Prop);
        }

        // The authority's word, as bytes, applied on the client.
        void Replicate()
        {
            Changes.Update(Authority.Entities, Layout, Identity, ++Generation);
            SnapshotWriteRequest write;
            write.Changes = &Changes;
            write.Layout = &Layout;
            write.Peer = &Peer;
            write.Tick = Authority.Last();
            write.Sequence = Peer.NextSnapshotSequence();
            write.CommandAck = CommandAck;
            const SnapshotWriteResult written = ReplicationWriteSnapshot(write, Scratch);
            ASSERT_TRUE(written.Ok);

            SnapshotApplyRequest apply;
            apply.Target = &Client.Entities;
            apply.Schema = &Schema;
            apply.Layout = &Layout;
            apply.Identity = &ClientIds;
            const SnapshotApplyResult applied =
                ReplicationApplySnapshot(apply, std::span(Scratch).subspan(0, written.BytesWritten));
            ASSERT_TRUE(applied.Ok()) << SnapshotApplyErrorToString(applied.Error);
            // As the engine does after every snapshot.
            Journal().Reconcile(Client.Entities, applied.CommandAck);
            Ack.Observe(applied.Sequence);
            Peer.Acknowledge(Ack);
        }

        // Gives the client's mirror what a prefab body would (the rig and facts
        // to gather), then aligns its clock so its next tick is the authority's
        // next one, `flight` ticks after the snapshot.
        void Join(std::uint64_t flight = 0)
        {
            Replicate();
            Mirror = ClientIds.TryResolve(Identity.TryFind(Prop));
            ASSERT_TRUE(Mirror.IsValid());
            ASSERT_NE(Mirror, Prop);
            Client.Entities.AddComponent(Mirror, AnimRig{ ClientRig });
            Client.Entities.AddComponent(Mirror, AnimFacts{});
            Client.Entities.AddComponent(Mirror, AnimTestMotion{});
            Client.Entities.AddComponent(Mirror, AnimDecisionLog{});
            const auto offset = static_cast<std::int64_t>(Authority.Now + flight)
                              - static_cast<std::int64_t>(ClientLocalTick);
            Client.Entities.SetResource(SimulationAuthority{ .Authoritative = false, .TickOffset = offset });
        }

        // One client tick, at whatever its own clock says the authority's
        // tick is.
        void ClientTick()
        {
            Client.Now = AuthorityTickOf(Client.Entities, ClientLocalTick);
            Client.Tick();
            ++ClientLocalTick;
        }

        // A tick on both: the authority's, a snapshot, then the client's for
        // the same authority tick.
        void Step()
        {
            Authority.Tick();
            Replicate();
            ClientTick();
        }

        void StepTo(AnimTick tick)
        {
            while (Authority.Now <= tick)
                Step();
        }

        AnimRequestResult Reload(int shells)
        {
            AnimRequestDesc desc;
            desc.Source = Prop;
            desc.Intent = Authority.Tag("anim.intent.reload");
            desc.Params[0] = AnimFactFromInt(shells);
            return IssueAnimRequest(Authority.Entities, Prop, desc, Authority.Now);
        }

        AnimRequestJournal& Journal() { return Client.Entities.GetResource<AnimRequestJournal>(); }

        // The client predicting a reload for its command `command`.
        AnimRequestResult Predict(AnimTick tick, std::uint64_t command)
        {
            AnimRequestDesc prediction;
            prediction.Source = Mirror;
            prediction.Intent = Client.Tag("anim.intent.reload");
            prediction.Params[0] = AnimFactFromInt(2);
            return Journal().Issue(Client.Entities, Mirror, prediction, tick, command);
        }

        const AnimRequestSet& ClientRequests() const
        {
            const World& reader = Client.Entities;
            return *reader.TryGet<AnimRequestSet>(Mirror);
        }

        static const AnimLayerFlow& Flow(AnimRigFixture& side, EntityId entity)
        {
            return side.Entities.TryGet<AnimFlowState>(entity)->Layers[0];
        }

        // Everything a late joiner has to arrive at, compared machine to
        // machine. Behavior tags are compared by name: the ids differ.
        void ExpectAgree(const char* when)
        {
            ASSERT_EQ(Client.Last(), Authority.Last()) << when;
            const AnimLayerContent& here = Authority.Playing(Prop);
            const AnimLayerContent& there = Client.Playing(Mirror);
            EXPECT_EQ(Authority.BehaviorName(Prop), Client.BehaviorName(Mirror)) << when;
            EXPECT_EQ(here.Content, there.Content) << when;
            EXPECT_FLOAT_EQ(here.TimeSeconds, there.TimeSeconds) << when;
            const AnimLayerFlow& flow = Flow(Authority, Prop);
            const AnimLayerFlow& mirrored = Flow(Client, Mirror);
            EXPECT_EQ(flow.Phase, mirrored.Phase) << when;
            EXPECT_EQ(flow.Section, mirrored.Section) << when;
            EXPECT_EQ(flow.SectionStartTick, mirrored.SectionStartTick) << when;
            EXPECT_EQ(flow.LoopCount, mirrored.LoopCount) << when;
        }
    };

    DataAssetHandle ReloadRigContent(AnimRigFixture& fx, std::string_view onCancel)
    {
        return LoadReloadRig(fx, CountedLoopFlow(), onCancel);
    }
}

// The request arrives naming what the client calls things: its own entity
// for the source, its own ids for the intent and a tag parameter.
TEST(AnimReplication, TheRequestArrivesInTheClientsOwnNames)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); });
    session.Authority.Tick();
    ASSERT_TRUE(session.Reload(3).Accepted());
    session.Authority.Tick();
    session.Join();

    const AnimRequest& sent = session.Authority.Entities.TryGet<AnimRequestSet>(session.Prop)->Records[0];
    const AnimRequest& arrived = session.Client.Entities.TryGet<AnimRequestSet>(session.Mirror)->Records[0];
    ASSERT_TRUE(arrived.Occupied);
    ASSERT_NE(sent.Intent, arrived.Intent) << "the machines must number tags differently for this to prove anything";
    EXPECT_EQ(session.Client.Tags().GetName(arrived.Intent), "anim.intent.reload");
    EXPECT_EQ(arrived.Id.Source, session.Mirror);
    EXPECT_EQ(arrived.Id.Sequence, sent.Id.Sequence);
    EXPECT_EQ(arrived.StartTick, sent.StartTick);
    EXPECT_EQ(AnimFactToInt(arrived.Params[0]), 3) << "a number travels as itself";
}

// A late joiner finds the section the authority is in, the loops it has
// played, and the time into it -- from the request's tick and anchor alone --
// and stays with the authority through the loops, the close and the idle.
TEST(AnimReplication, ALateJoinerReconstructsSectionsAndLoops)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); });
    session.Authority.Tick();
    ASSERT_TRUE(session.Reload(3).Accepted());
    session.Authority.Tick(49);
    ASSERT_EQ(session.Authority.Now, 50u);
    ASSERT_EQ(AnimSession::Flow(session.Authority, session.Prop).Section, 1u) << "joining inside the insert loop";

    // The snapshot takes three ticks to arrive; the authority moves on, and
    // the client's first tick is the authority's 53rd.
    session.Join(3);
    session.Authority.Tick(4);
    session.ClientTick();
    session.ExpectAgree("on the joiner's first tick");
    const AnimDecisionLog& log = session.Client.Log(session.Mirror);
    const AnimDecisionRecord* anchored = nullptr;
    for (std::size_t i = 0; i < log.Size() && anchored == nullptr; ++i)
        if (log.At(i).Cause == AnimDecisionCause::SectionChanged)
            anchored = &log.At(i);
    ASSERT_NE(anchored, nullptr);
    EXPECT_EQ(anchored->Reason, AnimChangeReason::FlowAnchored) << "entered where the anchor says, not at the top";

    for (const AnimTick tick : { 60u, 75u, 76u, 106u, 107u, 121u, 122u, 130u })
    {
        session.StepTo(tick);
        session.ExpectAgree(std::to_string(tick).c_str());
    }
    EXPECT_EQ(AnimSession::Flow(session.Client, session.Mirror).Phase, AnimFlowPhase::Complete);
}

// A cancelled request stays while its flow plays it out. A client that
// joins during that tail sees the close play, not an idle.
TEST(AnimReplication, ALateJoinerDuringATailSeesItPlayOut)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "finish"); });
    session.Authority.Tick();
    const AnimRequestResult reload = session.Reload(2);
    ASSERT_TRUE(reload.Accepted());
    session.Authority.Tick(19);
    ASSERT_TRUE(CancelAnimRequest(session.Authority.Entities, session.Prop, reload.Id, AnimCancelReason::Released,
                                  session.Authority.Now));
    // Well after the cancel: both inserts and the close still play.
    session.Authority.Tick(60);
    ASSERT_EQ(session.Authority.Now, 80u);
    ASSERT_EQ(session.Authority.BehaviorName(session.Prop), "anim.intent.reload");

    session.Join();
    session.ClientTick();
    session.Authority.Tick();
    session.ExpectAgree("joined inside the tail");
    const AnimRequest& arrived = session.Client.Entities.TryGet<AnimRequestSet>(session.Mirror)->Records[0];
    EXPECT_EQ(arrived.CancelReason, AnimCancelReason::Released);

    for (const AnimTick tick : { 85u, 90u, 91u, 92u, 95u })
    {
        session.StepTo(tick);
        session.ExpectAgree(std::to_string(tick).c_str());
    }
    EXPECT_EQ(session.Client.BehaviorName(session.Mirror), "Anim.Idle");

    // Where they settle, the desync probe finds them agreeing: the tail the
    // client kept for itself is the tail the authority kept.
    std::size_t cursor = 0;
    std::vector<NetDesyncSample> samples;
    session.StepTo(100);
    NetBuildDesyncReport(session.Changes, session.Layout, session.Peer, 0, cursor, samples);
    ASSERT_FALSE(samples.empty());
    const NetDesyncResult result =
        NetCheckDesyncReport(session.Client.Entities, session.Layout, session.ClientIds, nullptr, 0, samples);
    EXPECT_EQ(result.Compared, samples.size());
    EXPECT_EQ(result.Diverged, 0u);
}

namespace
{
    // Which binding fired and who it names as the instigator.
    struct EventRecorder
    {
        std::vector<std::pair<VerbBindingKey, EntityId>> Calls;

        VerbAdmission Invoke(const VerbInvocation& invocation)
        {
            Calls.emplace_back(invocation.Binding, invocation.Instigator);
            return VerbAdmission::Accepted;
        }
    };

    // The reload announcing itself: a gameplay event on entering the
    // behavior and a cosmetic one on each section.
    DataAssetHandle AnnouncingContent(AnimRigFixture& fx)
    {
        DataFieldSchema tag;
        tag.Key = "Tag";
        tag.Kind = DataFieldKind::GameplayTag;
        VerbDefinition definition;
        definition.Name = "test.announce";
        definition.Arguments.Children = { tag };
        VerbRegistrationScope scope(fx.Verbs(), "test");
        (void)scope.Declare(std::move(definition));
        EXPECT_TRUE(scope.Commit());
        (void)fx.Load("asset://anim/r.bindings.sdata", kVerbBindingsTypeName, R"({ "bindings": [
            { "key": "anim.reload.started", "verb": "test.announce", "inputs": [ "behavior" ],
              "arguments": { "Tag": { "input": "behavior" } } },
            { "key": "anim.reload.section", "verb": "test.announce", "inputs": [ "section" ],
              "arguments": { "Tag": { "input": "section" } } } ] })");
        std::string flow = CountedLoopFlow();
        flow.insert(flow.rfind('}'), R"(, "on_section_entered": { "binding": "anim.reload.section" })");
        return LoadReloadRig(fx, flow, "cancel_section", {},
                             R"(, "on_entered": { "binding": "anim.reload.started", "scope": "gameplay" })",
                             "asset://anim/r.bindings.sdata");
    }
}

// Gameplay events are the authority's alone. A client playing the same
// request produces the cosmetic ones, naming its own copy of the source.
TEST(AnimReplication, AClientNeverOriginatesGameplayEvents)
{
    AnimSession session(AnnouncingContent);
    EventRecorder authorityCalls;
    EventRecorder clientCalls;
    VerbDispatcher authorityVerbs(session.Authority.Verbs());
    VerbDispatcher clientVerbs(session.Client.Verbs());
    const VerbBindingToken authorityToken =
        authorityVerbs.Bind(session.Authority.Verbs().Find("test.announce"), authorityCalls);
    const VerbBindingToken clientToken = clientVerbs.Bind(session.Client.Verbs().Find("test.announce"), clientCalls);
    AnimEventSystem authorityEvents(&authorityVerbs, true);
    AnimEventSystem clientEvents(&clientVerbs, true);

    session.Authority.Tick();
    session.Join();
    ASSERT_TRUE(session.Reload(1).Accepted());
    for (int i = 0; i < 20; ++i)
    {
        session.Step();
        authorityEvents.Run(session.Authority.Entities, session.Authority.Last(), AnimRigFixture::kTick);
        clientEvents.Run(session.Client.Entities, session.Client.Last(), AnimRigFixture::kTick);
    }

    const VerbBindingKey started = MakeVerbBindingKey("anim.reload.started");
    const VerbBindingKey section = MakeVerbBindingKey("anim.reload.section");
    const auto count = [](const EventRecorder& recorder, VerbBindingKey key) {
        return std::ranges::count_if(recorder.Calls, [&](const auto& call) { return call.first == key; });
    };
    EXPECT_EQ(count(authorityCalls, started), 1);
    EXPECT_EQ(count(clientCalls, started), 0) << "a gameplay event produced on a client";
    EXPECT_EQ(count(clientCalls, section), count(authorityCalls, section));
    ASSERT_FALSE(clientCalls.Calls.empty());
    EXPECT_EQ(clientCalls.Calls.front().second, session.Mirror) << "the instigator is the client's own entity";
}

// A client that predicted a request is corrected by the authority's, which
// began later: from then on it plays what the authority plays, and what it
// already showed stays in its history rather than being rewritten.
TEST(AnimReplication, ACorrectionIsAbsorbedWithoutRewindingHistory)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); });
    session.Authority.Tick();
    session.Join();
    session.StepTo(9);

    // The client's prediction: the reload starts on tick 10.
    AnimRequestDesc prediction;
    prediction.Source = session.Mirror;
    prediction.Intent = session.Client.Tag("anim.intent.reload");
    prediction.Params[0] = AnimFactFromInt(2);
    ASSERT_TRUE(IssueAnimRequest(session.Client.Entities, session.Mirror, prediction, 10).Accepted());
    session.StepTo(11);
    ASSERT_EQ(session.Client.BehaviorName(session.Mirror), "anim.intent.reload");
    ASSERT_EQ(session.Authority.BehaviorName(session.Prop), "Anim.Idle");

    // The authority's word: it starts on tick 12.
    ASSERT_TRUE(session.Reload(2).Accepted());
    session.StepTo(12);
    session.ExpectAgree("on the correcting tick");
    for (const AnimTick tick : { 20u, 27u, 28u, 60u })
    {
        session.StepTo(tick);
        session.ExpectAgree(std::to_string(tick).c_str());
    }

    const AnimDecisionLog& log = session.Client.Log(session.Mirror);
    bool predictedChangeKept = false;
    for (std::size_t i = 0; i < log.Size(); ++i)
        predictedChangeKept = predictedChangeKept
                           || (log.At(i).Tick == 10 && log.At(i).Cause == AnimDecisionCause::ContentChanged);
    EXPECT_TRUE(predictedChangeKept) << "the change the client showed on tick 10 is still in its history";
}

// The news of a cancel arrives a flight late. The client enters the cancel
// section when it hears, and then moves to where the authority's anchor says
// that section began, rather than playing it late to the end.
TEST(AnimReplication, AClientHearingOfACancelLateFollowsTheAuthoritysAnchor)
{
    AnimSession session([](AnimRigFixture& fx) { return LoadReloadRig(fx, CountedLoopFlow("immediate")); });
    session.Authority.Tick();
    const AnimRequestResult reload = session.Reload(3);
    ASSERT_TRUE(reload.Accepted());
    session.Authority.Tick();
    session.Join();
    session.StepTo(29);
    session.ExpectAgree("inserting");

    ASSERT_TRUE(CancelAnimRequest(session.Authority.Entities, session.Prop, reload.Id, AnimCancelReason::Released,
                                  session.Authority.Now));
    // Three ticks with nothing arriving.
    for (int i = 0; i < 3; ++i)
    {
        session.Authority.Tick();
        session.ClientTick();
    }
    session.StepTo(33);
    session.ExpectAgree("the tick the cancel arrived");
    EXPECT_EQ(AnimSession::Flow(session.Client, session.Mirror).SectionStartTick, 30u);
    session.StepTo(50);
    session.ExpectAgree("through the cancel section");
}

// A prediction the authority confirms: its request replaces the guess, the
// journal forgets it, and the client plays what the authority plays.
TEST(AnimReplication, AConfirmedPredictionGivesWayToTheAuthoritysRequest)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); });
    session.Authority.Tick();
    session.Join();
    session.StepTo(9);
    ASSERT_TRUE(session.Predict(10, 100).Accepted());
    EXPECT_TRUE(session.ClientRequests().Records[0].Predicted);
    session.StepTo(11);
    EXPECT_EQ(session.Journal().Size(), 1u) << "undecided until the authority processes command 100";
    ASSERT_EQ(session.Client.BehaviorName(session.Mirror), "anim.intent.reload");

    // The authority processes command 100 on tick 12.
    ASSERT_TRUE(session.Reload(2).Accepted());
    session.CommandAck = 100;
    session.StepTo(12);
    EXPECT_EQ(session.Journal().Size(), 0u);
    EXPECT_FALSE(session.ClientRequests().Records[0].Predicted);
    for (const AnimTick tick : { 12u, 30u, 60u })
    {
        session.StepTo(tick);
        session.ExpectAgree(std::to_string(tick).c_str());
    }
}

// A prediction the authority refuses leaves no request behind, although the
// authority's set never changed and so was never sent again.
TEST(AnimReplication, ARefusedPredictionIsTakenDown)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); });
    session.Authority.Tick();
    session.Join();
    session.StepTo(9);
    ASSERT_TRUE(session.Predict(10, 100).Accepted());
    session.StepTo(11);
    ASSERT_EQ(session.Client.BehaviorName(session.Mirror), "anim.intent.reload");

    session.CommandAck = 100;
    session.StepTo(12);
    EXPECT_EQ(session.Journal().Size(), 0u);
    EXPECT_FALSE(session.ClientRequests().Records[0].Occupied);
    const AnimDecisionRecord* rebuilt = session.Client.LastRecord(session.Mirror, AnimDecisionCause::ContentChanged);
    ASSERT_NE(rebuilt, nullptr);
    EXPECT_EQ(rebuilt->Tick, 12u);
    EXPECT_EQ(rebuilt->Reason, AnimChangeReason::Reconstructed);
    // Idle is not request-driven, so its clock is this machine's own; what
    // has to agree is what plays.
    EXPECT_EQ(session.Client.BehaviorName(session.Mirror), "Anim.Idle");
    EXPECT_EQ(session.Client.Playing(session.Mirror).Content, session.Authority.Playing(session.Prop).Content);
}

// The authority's set arriving for another reason wipes the guess; one the
// authority has not decided yet is put back on top of it.
TEST(AnimReplication, AnUndecidedPredictionIsIssuedAgainOnTheAuthoritysSet)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); });
    const EntityId other = session.Authority.Entities.CreateEntity();
    session.Authority.Tick();
    session.Join();
    session.StepTo(9);
    ASSERT_TRUE(session.Predict(10, 100).Accepted());
    session.StepTo(10);

    AnimRequestDesc elsewhere;
    elsewhere.Source = other;
    elsewhere.Intent = session.Authority.Tag("anim.intent.reload");
    elsewhere.Lifetime = AnimRequestLifetime::Impulse;
    ASSERT_TRUE(IssueAnimRequest(session.Authority.Entities, session.Prop, elsewhere, session.Authority.Now).Accepted());
    session.StepTo(11);

    const AnimRequestSet& requests = session.ClientRequests();
    int authoritative = 0;
    int predicted = 0;
    for (const AnimRequest& request : requests.Records)
    {
        if (!request.Occupied)
            continue;
        ++(request.Predicted ? predicted : authoritative);
        if (request.Predicted)
        {
            EXPECT_EQ(request.StartTick, 10u) << "put back where the client guessed it";
        }
    }
    EXPECT_EQ(authoritative, 1);
    EXPECT_EQ(predicted, 1);
    EXPECT_EQ(session.Journal().Size(), 1u);
}

// On the authority there is nothing to predict: the journal issues and
// remembers nothing.
TEST(AnimReplication, TheAuthoritysJournalOnlyIssues)
{
    ReloadFlowFixture fx(CountedLoopFlow());
    AnimRequestJournal journal;
    AnimRequestDesc desc;
    desc.Source = fx.Entity;
    desc.Intent = fx.Tag("anim.intent.reload");
    ASSERT_TRUE(journal.Issue(fx.Entities, fx.Entity, desc, 0, 7).Accepted());
    EXPECT_EQ(journal.Size(), 0u);
    EXPECT_FALSE(static_cast<const World&>(fx.Entities).TryGet<AnimRequestSet>(fx.Entity)->Records[0].Predicted);
}

// The timing identity names things, so two machines numbering tags
// differently agree on it; it moves with timing and not with looks.
TEST(AnimReplication, TheTimingIdentityFollowsTimingNotLooks)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); });
    const std::uint64_t authority = session.Authority.Bound(session.AuthorityRig).TimingIdentity;
    EXPECT_NE(authority, 0u);
    EXPECT_EQ(session.Client.Bound(session.ClientRig).TimingIdentity, authority);

    // A blend is how it looks.
    session.Authority.Reload("asset://anim/f.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "cyclic", "blend": { "in": "crossfade", "in_ms": 250 } },
        { "tag": "Anim.Reload.Shell", "kind": "one_shot" },
        { "tag": "anim.intent.reload", "kind": "flow", "latch": { "on_request_cancel": "cancel_section" } } ] })");
    EXPECT_EQ(session.Authority.Bound(session.AuthorityRig).TimingIdentity, authority);

    // How a cancel plays out is what happens.
    session.Authority.Reload("asset://anim/f.behaviors.sdata", kAnimBehaviorSetType, R"({ "behaviors": [
        { "tag": "Anim.Idle", "kind": "cyclic" },
        { "tag": "Anim.Reload.Shell", "kind": "one_shot" },
        { "tag": "anim.intent.reload", "kind": "flow", "latch": { "on_request_cancel": "finish" } } ] })");
    EXPECT_NE(session.Authority.Bound(session.AuthorityRig).TimingIdentity, authority);
}

// A client whose rig plays a cancel out differently from the authority's
// says so, rather than reconstructing something else in silence.
TEST(AnimReplication, AClientWhoseTimingDiffersSaysSo)
{
    AnimSession session([](AnimRigFixture& fx) { return ReloadRigContent(fx, "cancel_section"); },
                        [](AnimRigFixture& fx) { return ReloadRigContent(fx, "finish"); });
    ASSERT_NE(session.Client.Bound(session.ClientRig).TimingIdentity,
              session.Authority.Bound(session.AuthorityRig).TimingIdentity);
    session.Authority.Tick();
    session.Join();
    session.StepTo(5);
    EXPECT_TRUE(session.Client.Entities.TryGet<AnimContentState>(session.Mirror)->TimingDisagrees);
    EXPECT_NE(session.Client.LastRecord(session.Mirror, AnimDecisionCause::TimingDisagreed), nullptr);
    EXPECT_FALSE(session.Authority.Entities.TryGet<AnimContentState>(session.Prop)->TimingDisagrees);
}

// A client's clock is its own count plus what it estimates the authority is
// ahead by. Content time is measured on the authority's count.
TEST(AnimReplication, AClientCountsContentTimeOnTheAuthoritysTicks)
{
    World world;
    EXPECT_EQ(AuthorityTickOf(world, 40), 40u) << "a World no session touched is its own authority";
    world.AddResource<SimulationAuthority>(SimulationAuthority{ .Authoritative = false, .TickOffset = -30 });
    EXPECT_EQ(AuthorityTickOf(world, 40), 10u);
    EXPECT_EQ(AuthorityTickOf(world, 20), 0u) << "floors rather than wrapping";
    world.GetResource<SimulationAuthority>().TickOffset = 7;
    EXPECT_EQ(AuthorityTickOf(world, 40), 47u);
}
