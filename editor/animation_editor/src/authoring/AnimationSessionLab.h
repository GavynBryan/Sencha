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
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct RuntimeAssets;

struct AnimationLabSettings
{
    AnimTick JoinTick = 0;
    std::uint32_t LatencyTicks = 0;
    // Dropped by a fixed pattern so a run repeats exactly.
    std::uint32_t LossPercent = 0;

    friend bool operator==(const AnimationLabSettings&, const AnimationLabSettings&) = default;
};

// A request the client predicts at Tick; the authority issues or refuses the
// same request at AuthorityTick.
struct AnimationLabInjection
{
    AnimTick Tick = 0;
    std::string Participant;
    std::string Intent;
    AnimTick AuthorityTick = 0;
    bool Confirmed = true;
};

struct AnimationLabTick
{
    AnimTick Tick = 0;
    std::uint32_t Delivered = 0;
    std::uint32_t Lost = 0;
    bool Joined = false;
    // Every layer plays the same behavior, content, time and flow section.
    bool Agrees = false;
    std::optional<std::size_t> FirstDisagreement;
    AnimationPoseResidual Pose;
    bool TimingDisagrees = false;
};

class AnimationSessionLab
{
public:
    AnimationSessionLab(const DataAssetCache& data, const AnimationClipCache* clips,
                        std::function<void(World&)> vocabulary, const SkeletonCache* skeletons);

    bool Open(AnimationScenario scenario, AnimationLabSettings settings,
              std::vector<AnimationLabInjection> injections = {});
    void Close();
    void RunTo(AnimTick tick);
    void Step();

    [[nodiscard]] bool IsOpen() const { return Opened; }
    [[nodiscard]] AnimTick Tick() const { return AuthoritySession.Tick(); }
    [[nodiscard]] const AnimationLabSettings& Settings() const { return Link; }
    [[nodiscard]] const std::vector<AnimationLabInjection>& Injections() const { return Injected; }
    [[nodiscard]] const AnimationPreviewSession& Authority() const { return AuthoritySession; }
    [[nodiscard]] const AnimationPreviewSession& Client() const { return ClientSession; }
    [[nodiscard]] const std::vector<AnimationLabTick>& Ticks() const { return Record; }
    // The client's requests after its first received snapshot.
    [[nodiscard]] const std::vector<AnimRequest>& JoinRequests() const { return Joined; }
    [[nodiscard]] std::optional<AnimTick> JoinedAt() const { return JoinedTick; }
    // The first tick from which every later tick agrees.
    [[nodiscard]] std::optional<AnimTick> ConvergedAt() const;
    [[nodiscard]] std::size_t PendingPredictions() const;
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

// The lab the author runs against the open scenario, with the settings and
// injections it runs under. The lab session is built on the first run.
class AnimationLabRun
{
public:
    AnimationLabRun(RuntimeAssets& assets, std::function<void(World&)> vocabulary)
        : Assets(assets)
        , Vocabulary(std::move(vocabulary))
    {
    }

    // Always from tick 0, so the result reflects the current edits.
    bool Run(const AnimationPreviewSession& simulation);

    AnimationLabSettings Settings;
    std::vector<AnimationLabInjection> Injections;
    AnimTick Tick = 300;
    std::unique_ptr<AnimationSessionLab> Session;

private:
    RuntimeAssets& Assets;
    std::function<void(World&)> Vocabulary;
};
