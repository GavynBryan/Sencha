#include "authoring/AnimationSessionLab.h"

#include <anim/AnimRequestJournal.h>
#include <anim/AnimationRegistration.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <net/NetReplicationComponents.h>
#include <world/ComponentRegistrar.h>

#include <algorithm>
#include <format>

namespace
{
    constexpr std::size_t kSnapshotBytes = 64 * 1024;

    std::string_view NameOf(const AnimationPreviewSession& session, GameplayTagId tag)
    {
        const GameplayTagRegistry* tags = session.Tags();
        return tags != nullptr && tag.IsValid() ? tags->GetName(tag) : std::string_view{};
    }
}

AnimationSessionLab::AnimationSessionLab(const DataAssetCache& data, const AnimationClipCache* clips,
                                         std::function<void(World&)> vocabulary, const SkeletonCache* skeletons)
    : AuthoritySession(data, clips, vocabulary, skeletons)
    , ClientSession(data, clips, vocabulary, skeletons)
    , Scratch(kSnapshotBytes)
{
    // Only what crosses the link: the marker and the animation vocabulary,
    // whose request set is the one component that travels.
    ComponentRegistrar components(&Schema, nullptr, &Layout);
    components.Add<NetReplicated>();
    RegisterAnimationComponents(components);
    Schema.Seal();
    Layout.Seal();
}

bool AnimationSessionLab::Open(AnimationScenario scenario, AnimationLabSettings settings,
                               std::vector<AnimationLabInjection> injections)
{
    Scenario = std::move(scenario);
    Link = settings;
    Injected = std::move(injections);
    // Commands are processed in the order the authority reaches them, and a
    // command's number is its place in that order.
    std::ranges::stable_sort(Injected, {}, &AnimationLabInjection::AuthorityTick);
    Restart();
    return Opened;
}

void AnimationSessionLab::Close()
{
    AuthoritySession.Close();
    ClientSession.Close();
    Opened = false;
    Record.clear();
    Joined.clear();
    JoinedTick.reset();
    InTransit.clear();
}

void AnimationSessionLab::Restart()
{
    AnimationScenario authority = Scenario;
    authority.Role = AnimationPreviewRole::Authority;
    AnimationScenario client = Scenario;
    client.Role = AnimationPreviewRole::Client;
    ClientSession.SetRequestsFromWire(true);
    Opened = AuthoritySession.Open(std::move(authority)) && ClientSession.Open(std::move(client));
    if (Opened)
        Connect();
}

void AnimationSessionLab::Connect()
{
    AuthorityIds = ReplicationAuthorityIdentity{};
    Changes.Reset();
    Peer = ReplicationPeerState{};
    ClientIds.Clear();
    Acks = NetSnapshotAck{};
    Generation = 0;
    CommandAck = 0;
    SnapshotsSent = 0;
    InTransit.clear();
    Record.clear();
    Joined.clear();
    JoinedTick.reset();

    // The subject and the participants are the same entities on both
    // machines, as a level's authored entities are: each client entity is
    // bound to the identity its authority counterpart is minted, so snapshots
    // land on it rather than spawning a copy.
    World& authority = *AuthoritySession.SimulationWorld();
    const auto pair = [&](EntityId here, EntityId there) {
        if (!here.IsValid() || !there.IsValid())
            return;
        authority.AddComponent<NetReplicated>(here);
        ClientIds.Bind(AuthorityIds.IdFor(here), there);
    };
    pair(AuthoritySession.Subject(), ClientSession.Subject());
    for (const std::string& name : Scenario.Participants)
        pair(AuthoritySession.ParticipantEntity(name), ClientSession.ParticipantEntity(name));
    Record.push_back(Compare(0));
}

void AnimationSessionLab::RunTo(AnimTick tick)
{
    if (!Opened)
        return;
    if (tick < Tick())
        Restart();
    while (Opened && Tick() < tick)
        Step();
}

void AnimationSessionLab::Step()
{
    if (!Opened)
        return;
    const AnimTick tick = AuthoritySession.NextTick();
    InjectAuthority(tick);
    AuthoritySession.Step();
    LostThisTick = 0;
    if (tick >= Link.JoinTick)
        Publish(tick);
    const std::uint32_t delivered = Deliver(tick);
    InjectClient(tick);
    ClientSession.Step();
    AnimationLabTick compared = Compare(tick);
    compared.Delivered = delivered;
    compared.Lost = LostThisTick;
    Record.push_back(compared);
}

void AnimationSessionLab::InjectAuthority(AnimTick tick)
{
    World& world = *AuthoritySession.SimulationWorld();
    const GameplayTagRegistry* tags = AuthoritySession.Tags();
    for (std::size_t i = 0; i < Injected.size(); ++i)
    {
        const AnimationLabInjection& injection = Injected[i];
        if (injection.AuthorityTick != tick)
            continue;
        CommandAck = std::max<std::uint64_t>(CommandAck, i + 1);
        if (!injection.Confirmed || tags == nullptr)
            continue;
        AnimRequestDesc desc;
        desc.Source = AuthoritySession.ParticipantEntity(injection.Participant);
        desc.Intent = tags->FindTag(injection.Intent);
        (void)IssueAnimRequest(world, AuthoritySession.Subject(), desc, tick);
    }
}

void AnimationSessionLab::InjectClient(AnimTick tick)
{
    World& world = *ClientSession.SimulationWorld();
    AnimRequestJournal* journal = world.TryGetResource<AnimRequestJournal>();
    const GameplayTagRegistry* tags = ClientSession.Tags();
    if (journal == nullptr || tags == nullptr)
        return;
    for (std::size_t i = 0; i < Injected.size(); ++i)
    {
        const AnimationLabInjection& injection = Injected[i];
        if (injection.Tick != tick)
            continue;
        AnimRequestDesc desc;
        desc.Source = ClientSession.ParticipantEntity(injection.Participant);
        desc.Intent = tags->FindTag(injection.Intent);
        (void)journal->Issue(world, ClientSession.Subject(), desc, tick, i + 1);
    }
}

void AnimationSessionLab::Publish(AnimTick tick)
{
    Changes.Update(*AuthoritySession.SimulationWorld(), Layout, AuthorityIds, ++Generation);
    SnapshotWriteRequest write;
    write.Changes = &Changes;
    write.Layout = &Layout;
    write.Peer = &Peer;
    write.Tick = tick;
    write.Sequence = Peer.NextSnapshotSequence();
    write.CommandAck = CommandAck;
    const SnapshotWriteResult written = ReplicationWriteSnapshot(write, Scratch);
    if (!written.Ok)
        return;
    // A fixed scatter over each hundred sends, so a loss rate drops the same
    // snapshots on every run.
    const bool lost = (SnapshotsSent * 37u + 11u) % 100u < Link.LossPercent;
    ++SnapshotsSent;
    if (lost)
    {
        ++LostThisTick;
        return;
    }
    InTransit.push_back(InFlight{ .Arrives = tick + Link.LatencyTicks,
                                  .Bytes = std::vector<std::byte>(Scratch.begin(),
                                                                  Scratch.begin() + written.BytesWritten) });
}

std::uint32_t AnimationSessionLab::Deliver(AnimTick tick)
{
    World& world = *ClientSession.SimulationWorld();
    std::uint32_t delivered = 0;
    while (!InTransit.empty() && InTransit.front().Arrives <= tick)
    {
        SnapshotApplyRequest apply;
        apply.Target = &world;
        apply.Schema = &Schema;
        apply.Layout = &Layout;
        apply.Identity = &ClientIds;
        const SnapshotApplyResult applied = ReplicationApplySnapshot(apply, InTransit.front().Bytes);
        InTransit.pop_front();
        if (!applied.Ok())
            continue;
        ++delivered;
        if (AnimRequestJournal* journal = world.TryGetResource<AnimRequestJournal>())
            journal->Reconcile(world, applied.CommandAck);
        Acks.Observe(applied.Sequence);
        Peer.Acknowledge(Acks);
        if (!JoinedTick.has_value())
        {
            JoinedTick = tick;
            if (const AnimRequestSet* set = ClientSession.Requests())
                for (const AnimRequest& request : set->Records)
                    if (request.Occupied)
                        Joined.push_back(request);
        }
    }
    return delivered;
}

AnimationLabTick AnimationSessionLab::Compare(AnimTick tick) const
{
    AnimationLabTick compared;
    compared.Tick = tick;
    compared.Joined = JoinedTick.has_value();
    if (const AnimContentState* content = ClientSession.Content())
        compared.TimingDisagrees = content->TimingDisagrees;
    if (AuthoritySession.History().empty() || ClientSession.History().empty())
        return compared;
    const AnimationPreviewTickRecord& here = AuthoritySession.History().back();
    const AnimationPreviewTickRecord& there = ClientSession.History().back();
    compared.Pose = AnimationPoseDifference(tick, here.Pose, there.Pose);
    compared.Agrees = here.Layers.size() == there.Layers.size();
    for (std::size_t l = 0; compared.Agrees && l < here.Layers.size(); ++l)
    {
        const AnimationPreviewLayerRecord& a = here.Layers[l];
        const AnimationPreviewLayerRecord& b = there.Layers[l];
        const bool same = NameOf(AuthoritySession, a.Behavior) == NameOf(ClientSession, b.Behavior)
                       && a.Content == b.Content && a.Clip == b.Clip && a.TimeSeconds == b.TimeSeconds
                       && a.Flow.Phase == b.Flow.Phase && a.Flow.Section == b.Flow.Section
                       && a.Flow.SectionStartTick == b.Flow.SectionStartTick;
        if (!same)
        {
            compared.Agrees = false;
            compared.FirstDisagreement = l;
        }
    }
    return compared;
}

std::optional<AnimTick> AnimationSessionLab::ConvergedAt() const
{
    if (Record.empty() || !Record.back().Agrees)
        return std::nullopt;
    std::size_t first = Record.size() - 1;
    while (first > 0 && Record[first - 1].Agrees)
        --first;
    return Record[first].Tick;
}

std::size_t AnimationSessionLab::PendingPredictions() const
{
    const World* world = ClientSession.SimulationWorld();
    const AnimRequestJournal* journal = world != nullptr ? world->TryGetResource<AnimRequestJournal>() : nullptr;
    return journal != nullptr ? journal->Size() : 0;
}

std::string AnimationSessionLab::Status() const
{
    const std::string joined = JoinedTick.has_value() ? std::format("joined on tick {}", *JoinedTick)
                                                      : std::format("joins from tick {}", Link.JoinTick);
    return std::format("Authority runs the scenario. Client receives its requests over the link ({}, {} ticks "
                       "latency, {}% loss). Facts are synthetic scenario inputs on both.",
                       joined, Link.LatencyTicks, Link.LossPercent);
}

std::vector<AnimDiagnostic> AnimationSessionLab::Problems() const
{
    std::vector<AnimDiagnostic> problems = AuthoritySession.Problems();
    for (const AnimDiagnostic& problem : ClientSession.Problems())
        if (std::ranges::find_if(problems, [&](const AnimDiagnostic& held) {
                return held.Code == problem.Code && held.Message == problem.Message;
            }) == problems.end())
            problems.push_back(problem);
    return problems;
}
