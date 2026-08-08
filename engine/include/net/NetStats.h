#pragma once

#include <net/NetProtocol.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

//=============================================================================
// NetStats
//
// What a session is spending, as rates rather than totals.
//
// Every defect this epic has found so far was found by playing rather than by
// measuring, and two of them were invisible to a green suite: a mirrored pawn
// whose state was correct but which nothing drew, and a client whose input was
// correct but permanently late. Both were cheap to see and expensive to
// stumble into. A rate surface is the difference between "it works" and "it
// works at the cost you think", while it is happening rather than afterwards.
//
// Counting only. It decides nothing and is read by nothing that decides, which
// is what lets the frame record into it wherever the traffic actually is
// without raising an ordering question.
//=============================================================================

// Accounting buckets, deliberately the payload kinds: "which traffic grew" is
// the first question anyone asks, and per-message detail is a capture's job.
enum class NetTrafficKind : std::uint8_t
{
    Snapshot,
    Command,
    CVar,
    // Handshake, keepalives, and anything a game sends for itself.
    Other,
};

inline constexpr std::size_t kNetTrafficKinds = 4;

[[nodiscard]] std::string_view NetTrafficKindToString(NetTrafficKind kind);

// The bucket a payload counts against. A kind this build does not know counts
// as Other rather than being dropped: unattributed traffic is still traffic,
// and a rate that quietly omitted it would read as free.
[[nodiscard]] NetTrafficKind NetTrafficKindOf(NetPayloadKind payload);

struct NetTrafficRate
{
    double Messages = 0.0;
    double Bytes = 0.0;
};

class NetStats
{
public:
    // A minute of history: long enough to watch a leak build, short enough to
    // stay readable at a glance.
    static constexpr std::size_t kHistory = 60;
    static constexpr double kWindowSeconds = 1.0;

    void RecordIn(NetTrafficKind kind, std::size_t bytes,
                  std::uint32_t messages = 1);
    void RecordOut(NetTrafficKind kind, std::size_t bytes,
                   std::uint32_t messages = 1);

    // Closes the accounting window if one has elapsed, and reports rates over
    // however long it actually was rather than over the nominal second -- a
    // frame that hitched past the window would otherwise read as a traffic
    // spike it never was.
    //
    // Takes the unscaled wall clock: a paused or time-scaled simulation still
    // sends and receives at wall rate.
    void Sample(double nowSeconds);

    [[nodiscard]] NetTrafficRate In(NetTrafficKind kind) const;
    [[nodiscard]] NetTrafficRate Out(NetTrafficKind kind) const;
    [[nodiscard]] NetTrafficRate TotalIn() const;
    [[nodiscard]] NetTrafficRate TotalOut() const;

    [[nodiscard]] std::uint64_t LifetimeBytesIn() const { return LifetimeIn; }
    [[nodiscard]] std::uint64_t LifetimeBytesOut() const { return LifetimeOut; }

    // Bytes per second per closed window, oldest first, for a plot. Shorter
    // than kHistory until that many windows have closed.
    [[nodiscard]] std::span<const float> BytesInHistory() const;
    [[nodiscard]] std::span<const float> BytesOutHistory() const;

    // Session-transient, like everything else about a connection.
    void Reset();

private:
    struct Counter
    {
        std::uint64_t Messages = 0;
        std::uint64_t Bytes = 0;
    };

    void Push(std::array<float, kHistory>& series, float value);

    std::array<Counter, kNetTrafficKinds> WindowIn{};
    std::array<Counter, kNetTrafficKinds> WindowOut{};
    std::array<NetTrafficRate, kNetTrafficKinds> RatesIn{};
    std::array<NetTrafficRate, kNetTrafficKinds> RatesOut{};

    // Oldest first, so a plot can take them as they are. Shifted rather than
    // ringed: sixty floats once a second is not worth an index to reason about.
    std::array<float, kHistory> SeriesIn{};
    std::array<float, kHistory> SeriesOut{};
    std::size_t Closed = 0;

    std::uint64_t LifetimeIn = 0;
    std::uint64_t LifetimeOut = 0;

    // The first Sample only starts the clock. Without this the first window
    // would run from time zero and report a rate averaged over the whole
    // process lifetime.
    double WindowStarted = 0.0;
    bool Started = false;
};
