#pragma once

#include "authoring/AnimationBlendComparison.h"
#include "authoring/AnimationPreviewSession.h"

#include <ecs/WorldComponentSchema.h>
#include <net/NetSnapshotAck.h>
#include <net/ReplicationChangeStore.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationSnapshot.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// AnimationSessionLab
//
// A rig under a scenario on two machines at once: an authority session that
// runs the scenario, and a client session that receives its requests only as
// snapshot bytes -- the production change store, snapshot writer and
// applier, request codec and prediction journal -- carried across a link with
// a delay and a loss pattern. Everything the client plays it reconstructs from
// what arrived.
//
// Both sessions run the same scenario facts on the same ticks: the facts are
// synthetic inputs, standing in for replicated gameplay. Only requests cross
// the link, and the client hears nothing before it joins. Its clock is the
// authority's: latency is modelled by delivery, not by a clock offset, and
// acknowledgements return at once.
//
// Nothing here writes an asset; each run starts from tick 0.
//=============================================================================

struct AnimationLabSettings
{
    // The first tick the authority sends this client anything.
    AnimTick JoinTick = 0;
    // How many ticks a snapshot spends on the way.
    std::uint32_t LatencyTicks = 0;
    // Of every hundred snapshots, how many never arrive, chosen by a fixed
    // pattern so a run repeats exactly.
    std::uint32_t LossPercent = 0;

    friend bool operator==(const AnimationLabSettings&, const AnimationLabSettings&) = default;
};

// A request the client predicts, and what the authority makes of it.
struct AnimationLabInjection
{
    // When the client issues its guess.
    AnimTick Tick = 0;
    std::string Participant;
    std::string Intent;
    // When the authority processes the command behind it: it issues the same
    // request then if Confirmed, and refuses it otherwise.
    AnimTick AuthorityTick = 0;
    bool Confirmed = true;
};

// One tick of both machines, compared.
struct AnimationLabTick
{
    AnimTick Tick = 0;
    // Snapshots that arrived before the client's tick ran, and that the link
    // lost on their way here this tick.
    std::uint32_t Delivered = 0;
    std::uint32_t Lost = 0;
    bool Joined = false;
    // Every layer plays the same behavior and content at the same time, in
    // the same section: the client arrived where the authority is.
    bool Agrees = false;
    // The first layer that does not, when one does not.
    std::optional<std::size_t> FirstDisagreement;
    AnimationPoseResidual Pose;
    bool TimingDisagrees = false;
};

class AnimationSessionLab
{
public:
    AnimationSessionLab(const DataAssetCache& data, const AnimationClipCache* clips,
                        std::function<void(World&)> vocabulary, const SkeletonCache* skeletons);

    // Opens both sessions on `scenario` and runs tick 0. False when the rig
    // does not open; Problems() says why.
    bool Open(AnimationScenario scenario, AnimationLabSettings settings,
              std::vector<AnimationLabInjection> injections = {});
    void Close();
    // Runs both to `tick`, from tick 0 when it is behind the current one.
    void RunTo(AnimTick tick);
    void Step();

    [[nodiscard]] bool IsOpen() const { return Opened; }
    [[nodiscard]] AnimTick Tick() const { return AuthoritySession.Tick(); }
    [[nodiscard]] const AnimationLabSettings& Settings() const { return Link; }
    [[nodiscard]] const std::vector<AnimationLabInjection>& Injections() const { return Injected; }
    [[nodiscard]] const AnimationPreviewSession& Authority() const { return AuthoritySession; }
    [[nodiscard]] const AnimationPreviewSession& Client() const { return ClientSession; }
    [[nodiscard]] const std::vector<AnimationLabTick>& Ticks() const { return Record; }
    // The requests the client held after the first snapshot it received: what
    // a joiner starts from.
    [[nodiscard]] const std::vector<AnimRequest>& JoinRequests() const { return Joined; }
    [[nodiscard]] std::optional<AnimTick> JoinedAt() const { return JoinedTick; }
    // The first tick from which every later tick agrees, if the run has one.
    [[nodiscard]] std::optional<AnimTick> ConvergedAt() const;
    // The client's predictions still waiting on the authority.
    [[nodiscard]] std::size_t PendingPredictions() const;
    // One line naming each machine's role and where its inputs come from.
    [[nodiscard]] std::string Status() const;
    [[nodiscard]] std::vector<AnimDiagnostic> Problems() const;

private:
    struct InFlight
    {
        AnimTick Arrives = 0;
        std::vector<std::byte> Bytes;
    };

    void Restart();
    void Connect();
    void Publish(AnimTick tick);
    std::uint32_t Deliver(AnimTick tick);
    void InjectAuthority(AnimTick tick);
    void InjectClient(AnimTick tick);
    AnimationLabTick Compare(AnimTick tick) const;

    AnimationPreviewSession AuthoritySession;
    AnimationPreviewSession ClientSession;
    AnimationScenario Scenario;
    AnimationLabSettings Link;
    std::vector<AnimationLabInjection> Injected;
    bool Opened = false;

    WorldComponentSchema Schema;
    ReplicationLayout Layout;
    ReplicationAuthorityIdentity AuthorityIds;
    ReplicationChangeStore Changes;
    ReplicationPeerState Peer;
    ReplicationClientIdentity ClientIds;
    NetSnapshotAck Acks;
    std::uint64_t Generation = 0;
    std::uint64_t CommandAck = 0;
    std::uint32_t SnapshotsSent = 0;
    std::deque<InFlight> InTransit;
    std::uint32_t LostThisTick = 0;
    std::vector<std::byte> Scratch;

    std::vector<AnimationLabTick> Record;
    std::vector<AnimRequest> Joined;
    std::optional<AnimTick> JoinedTick;
};
