#pragma once

#include <core/console/ConsoleTypes.h>
#include <net/NetSession.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class ConsoleRegistry;

//=============================================================================
// NetCVarSync
//
// Carries the value of a session-owned cvar from the authority to its peers.
//
// The rule the enforcement half implements is "a client may not set this"; this
// is the other half, without which a client would simply be stuck with whatever
// it had. Both are needed for the value to actually be the session's: refusing
// a write without supplying the right answer leaves two machines disagreeing
// just as much as allowing it.
//
// Rides the reliable channel. A snapshot can afford to be lost because the next
// one supersedes it; a cvar update has no next one, and a client that missed
// the tick rate changing would be wrong until something else happened to change
// it again.
//
// Only names the authority already published are accepted. A client never
// registers a cvar because a server mentioned it, which keeps a hostile
// authority from growing a client's console or shadowing a local name.
//=============================================================================

// One name and value, in the flat form the wire carries. Strings are capped
// like every other length here; nothing is sized by what a peer claimed.
struct NetCVarUpdate
{
    std::string Name;
    CVarType Type = CVarType::Bool;
    bool BoolValue = false;
    std::int64_t IntValue = 0;
    double DoubleValue = 0.0;
    std::string StringValue;
};

inline constexpr std::size_t kNetMaxCVarNameBytes = 64;
inline constexpr std::size_t kNetMaxCVarStringBytes = 256;

// Encodes into `out`, which must already carry the payload-kind byte. Returns
// the bytes used, or zero if it would not fit.
[[nodiscard]] std::size_t NetEncodeCVarUpdate(const NetCVarUpdate& update,
                                              std::span<std::byte> out);

// Decodes one update. Returns false on truncation, an unknown type, or a length
// past its cap.
[[nodiscard]] bool NetDecodeCVarUpdate(std::span<const std::byte> bytes,
                                       NetCVarUpdate& out);

// Authority: sends every Replicated cvar whose value has changed since the last
// call, and every one of them to a peer that has not been sent any yet. Returns
// how many updates were queued.
//
// Held per peer rather than broadcast blindly so a peer that joins mid-session
// gets the current values without the authority resending everything to
// everyone whenever one changes.
class NetCVarPublisher
{
public:
    struct PublishStats
    {
        std::size_t Updates = 0;
        std::size_t BytesQueued = 0;
    };

    PublishStats Publish(NetSession& session, const ConsoleRegistry& registry);
    void ForgetPeer(PeerId peer);
    void Reset() { Sent.clear(); }

private:
    // What each peer was last told, by cvar name. A peer with no entry is new
    // and gets everything.
    std::unordered_map<PeerId,
                       std::unordered_map<std::string, std::string>> Sent;
};

// Client: applies one update, if the named cvar exists here and the session
// owns it. Returns false when the name is unknown or not the session's to set,
// which is a protocol violation rather than something to tolerate quietly.
[[nodiscard]] bool NetApplyCVarUpdate(ConsoleRegistry& registry,
                                      const NetCVarUpdate& update);
