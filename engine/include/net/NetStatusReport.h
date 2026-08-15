#pragma once

#include <net/NetSession.h>
#include <net/ReplicationSnapshot.h>

#include <string>

class ClientPrediction;
class NetStats;
class NetTickEstimator;
class PeerCommandRuntime;
class ReplicationInterpolation;
class ReplicationLayout;
class ReplicationRuntime;
class World;

//=============================================================================
// NetStatusReport
//
// What a session is doing, as text.
//
// There was already an account of this, and it was an ImGui panel. A dedicated
// host runs with no graphics API and no overlay, so the one process where
// budget occupancy, deferral age, unsendable entities, and per-peer queue depth
// decide whether a session is healthy could see none of them -- while a listen
// host, where they matter least, could see all of them. This is that account in
// a form a terminal can carry.
//
// Not a second set of numbers. Everything here is already computed by the
// publish walk, the channels, and the command buffers; this only reaches them.
// The panel keeps its own layout because a panel is sliders and tooltips and
// plots, which text is not, but neither of them owns a counter.
//
// Text rather than a struct of numbers, because the consumer is a person
// reading a terminal. A caller that wants the numbers should read the sources.
//=============================================================================

// Everything the report reads, all optional. A null source is a section
// omitted, which is what a test rig with no replication and what a client with
// no prediction both look like -- and neither should have to assemble the
// others to ask.
struct NetStatusSources
{
    const NetSession* Session = nullptr;
    const NetStats* Traffic = nullptr;
    const ReplicationRuntime* Replication = nullptr;
    const PeerCommandRuntime* Commands = nullptr;
    const ClientPrediction* Prediction = nullptr;
    const ReplicationInterpolation* Interpolation = nullptr;
    const NetTickEstimator* Clock = nullptr;
};

// Multi-line, no trailing newline. A session that does not exist reports that
// rather than an empty page.
[[nodiscard]] std::string NetFormatStatus(const NetStatusSources& sources);

//-----------------------------------------------------------------------------
// One object
//
// "Why is this not replicating" was answerable only by reading the writer. Every
// fact it needs was already recorded -- when each field run last moved, how far
// each peer has proved it has got, what is still owed and why -- and none of it
// was reachable. This reaches it, and it applies the writer's own owed-field
// rule rather than a second copy of it, so it cannot answer confidently and
// wrongly.
//-----------------------------------------------------------------------------
struct NetEntityReportSources
{
    const NetSession* Session = nullptr;
    const ReplicationRuntime* Replication = nullptr;
    const ReplicationLayout* Layout = nullptr;
};

// `focus` invalid reports every peer; naming one reports that peer's owed
// fields by name, which is the "why can that player not see this" form.
[[nodiscard]] std::string NetFormatEntity(const NetEntityReportSources& sources,
                                          NetEntityId id, PeerId focus);

//-----------------------------------------------------------------------------
// Network ownership
//
// Straight off the NetOwner column, which is the only record of it. Participant
// and control state are reported by participant_status at their owning layer.
//-----------------------------------------------------------------------------
[[nodiscard]] std::string NetFormatOwners(const NetSession* session,
                                          const World& entities,
                                          const ReplicationRuntime* replication);

//-----------------------------------------------------------------------------
// Which rooms each peer is holding
//
// The one question a streamed session raises that nothing else answers: an
// entity that is not arriving may be behind a budget, a floor, or a zone the
// peer has not acked, and only the last of those is invisible from every other
// readout. On a host this is per peer; on a client it is what this machine was
// granted and what it has confirmed.
//-----------------------------------------------------------------------------
[[nodiscard]] std::string NetFormatZones(const NetSession* session,
                                         const ReplicationRuntime* replication);
