#pragma once

#include <authored/VerbArguments.h>
#include <authored/VerbDispatcher.h>
#include <ecs/EntityId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class AssetRegistry;
class ConsoleRegistry;
class DataAssetCache;
class EngineSchedule;
class Logger;
class LoggingProvider;
class World;
struct FixedLogicContext;

//=============================================================================
// VerbRelaySystem
//
// The one way a placed relay is activated, and the one place a relay's
// activation becomes an invocation.
//
// Activation is an ingress, not a second vocabulary: a native gameplay path
// names the relay entity and hands over typed input values, and that is all
// it can say. There is no string naming a subsystem here and no callback to
// register. What the relay does is its binding's business.
//
// Requests are admitted into a bounded queue and drained at fixed logic, so a
// relay activated from a frame-rate path -- a console command, a UI drain, an
// overlap the physics step reports -- acts on the simulation clock, once, in
// admission order. The drain takes the batch that was present when it began;
// a request admitted while that batch is processed waits for the next tick,
// which is what keeps one tick from running an unbounded relay chain.
//
// Contract, per request at the drain:
//   - the relay entity must still be alive and still carry VerbRelay;
//   - its partition must be in the tick's logic set (a dormant zone's relay
//     does not fire, and does not wait);
//   - its binding must resolve against the current catalog and asset revision.
// Anything else abandons the request, recorded on the trace ring when there is
// one, and the drain moves on. A one-shot request never waits for a later
// incarnation of its target.
//
// Capacity is `logic.relay.queue_capacity`. Overflow refuses admission
// visibly; nothing that was accepted is dropped. Shutdown drops whatever is
// still queued, because the dispatcher the requests were headed for is gone.
//=============================================================================
class VerbRelaySystem
{
public:
    VerbRelaySystem(VerbDispatcher& dispatcher, DataAssetCache& dataAssets, Logger& log);

    // Admits one activation. Accepted means it will be offered to the relay's
    // binding at the next drain; QueueFull means it will not, and the caller
    // knows now. The relay's liveness is checked at the drain, not here: the
    // entity may legitimately be created and activated in one frame.
    //
    // `instigator` is the participant behind the activation when the native
    // path knows one -- the player who stepped on the plate, the peer whose
    // request the authority answered -- and is carried to the operation as
    // provenance, never as a target. The tick is stamped at the drain.
    [[nodiscard]] VerbAdmission Activate(EntityId relay,
                                         std::span<const VerbValue> inputs,
                                         EntityId instigator = {},
                                         InvocationId parent = {});

    void SetCapacity(std::size_t capacity);
    [[nodiscard]] std::size_t Capacity() const { return QueueCapacity; }
    [[nodiscard]] std::size_t Pending() const { return Queue.size(); }

    void FixedLogic(FixedLogicContext& ctx);
    void Shutdown();

    struct Counters
    {
        std::uint64_t Admitted = 0;
        std::uint64_t Refused = 0;
        std::uint64_t Invoked = 0;
        std::uint64_t Abandoned = 0;
    };
    [[nodiscard]] const Counters& Stats() const { return Counts; }

private:
    struct Request
    {
        EntityId Relay;
        EntityId Instigator;
        InvocationId Parent;
        std::vector<VerbValue> Inputs;
    };

    void Abandon(const Request& request, VerbAdmission why);

    VerbDispatcher& Dispatcher;
    DataAssetCache& DataAssets;
    Logger& Log;

    std::vector<Request> Queue;
    // Swapped with Queue at the start of a drain, so admissions made while it
    // runs land in the next batch. Kept so the vectors trade capacity rather
    // than reallocating.
    std::vector<Request> Batch;
    std::size_t QueueCapacity = 64;
    Counters Counts;
};

// Composes the relay: the World resource its drain resolves through, the
// system itself, and the cvar that sizes its queue. Called by the host that
// owns the dispatcher, once, before any content that places a relay is loaded.
VerbRelaySystem& RegisterVerbRelaySystem(EngineSchedule& schedule,
                                         World& world,
                                         VerbDispatcher& dispatcher,
                                         const AssetRegistry& assets,
                                         DataAssetCache& dataAssets,
                                         ConsoleRegistry& console,
                                         LoggingProvider& logging);

// Gives back every lease the relay's binding store holds into the content
// stack. Called by the host before that stack goes: the store is a World
// resource and the World outlives the caches, so a lease left here would be
// released against a destroyed owner.
void DisconnectVerbRelays(World& world);
