#pragma once

#include <string>

class ClientPrediction;
class NetSession;
class NetStats;
class NetTickEstimator;
class PeerCommandRuntime;
class ReplicationInterpolation;
class ReplicationRuntime;

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
