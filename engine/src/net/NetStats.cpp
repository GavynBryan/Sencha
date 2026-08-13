#include <net/NetStats.h>

#include <algorithm>

namespace
{
    [[nodiscard]] std::size_t Slot(NetTrafficKind kind)
    {
        const std::size_t index = static_cast<std::size_t>(kind);
        return index < kNetTrafficKinds
                   ? index
                   : static_cast<std::size_t>(NetTrafficKind::Other);
    }
}

std::string_view NetTrafficKindToString(NetTrafficKind kind)
{
    switch (kind)
    {
    case NetTrafficKind::Snapshot: return "snapshot";
    case NetTrafficKind::Command:  return "command";
    case NetTrafficKind::CVar:     return "cvar";
    case NetTrafficKind::Zone:     return "zone";
    case NetTrafficKind::Game:     return "game";
    case NetTrafficKind::Other:    break;
    }
    return "other";
}

NetTrafficKind NetTrafficKindOf(NetPayloadKind payload)
{
    switch (payload)
    {
    case NetPayloadKind::Snapshot: return NetTrafficKind::Snapshot;
    case NetPayloadKind::Command:  return NetTrafficKind::Command;
    case NetPayloadKind::CVar:     return NetTrafficKind::CVar;
    case NetPayloadKind::ZoneScope:
    case NetPayloadKind::ZoneAck:  return NetTrafficKind::Zone;
    case NetPayloadKind::Invalid:  break;
    }
    return NetTrafficKind::Other;
}

NetTrafficKind NetTrafficKindOf(std::uint8_t payloadKind)
{
    if (payloadKind >= kNetFirstGamePayloadKind)
        return NetTrafficKind::Game;
    return NetTrafficKindOf(static_cast<NetPayloadKind>(payloadKind));
}

void NetStats::RecordIn(NetTrafficKind kind, std::size_t bytes,
                        std::uint32_t messages)
{
    Counter& counter = WindowIn[Slot(kind)];
    counter.Messages += messages;
    counter.Bytes += bytes;
    LifetimeIn += bytes;
}

void NetStats::RecordOut(NetTrafficKind kind, std::size_t bytes,
                         std::uint32_t messages)
{
    Counter& counter = WindowOut[Slot(kind)];
    counter.Messages += messages;
    counter.Bytes += bytes;
    LifetimeOut += bytes;
}

void NetStats::Push(std::array<float, kHistory>& series, float value)
{
    std::rotate(series.begin(), series.begin() + 1, series.end());
    series[kHistory - 1] = value;
}

void NetStats::Sample(double nowSeconds)
{
    if (!Started)
    {
        Started = true;
        WindowStarted = nowSeconds;
        return;
    }

    const double elapsed = nowSeconds - WindowStarted;
    if (elapsed < kWindowSeconds)
        return;

    const double perSecond = elapsed > 0.0 ? 1.0 / elapsed : 0.0;

    double bytesIn = 0.0;
    double bytesOut = 0.0;
    for (std::size_t kind = 0; kind < kNetTrafficKinds; ++kind)
    {
        RatesIn[kind].Messages =
            static_cast<double>(WindowIn[kind].Messages) * perSecond;
        RatesIn[kind].Bytes =
            static_cast<double>(WindowIn[kind].Bytes) * perSecond;
        RatesOut[kind].Messages =
            static_cast<double>(WindowOut[kind].Messages) * perSecond;
        RatesOut[kind].Bytes =
            static_cast<double>(WindowOut[kind].Bytes) * perSecond;

        bytesIn += RatesIn[kind].Bytes;
        bytesOut += RatesOut[kind].Bytes;

        WindowIn[kind] = Counter{};
        WindowOut[kind] = Counter{};
    }

    Push(SeriesIn, static_cast<float>(bytesIn));
    Push(SeriesOut, static_cast<float>(bytesOut));
    Closed = std::min(Closed + 1, kHistory);

    WindowStarted = nowSeconds;
}

NetTrafficRate NetStats::In(NetTrafficKind kind) const
{
    return RatesIn[Slot(kind)];
}

NetTrafficRate NetStats::Out(NetTrafficKind kind) const
{
    return RatesOut[Slot(kind)];
}

NetTrafficRate NetStats::TotalIn() const
{
    NetTrafficRate total;
    for (const NetTrafficRate& rate : RatesIn)
    {
        total.Messages += rate.Messages;
        total.Bytes += rate.Bytes;
    }
    return total;
}

NetTrafficRate NetStats::TotalOut() const
{
    NetTrafficRate total;
    for (const NetTrafficRate& rate : RatesOut)
    {
        total.Messages += rate.Messages;
        total.Bytes += rate.Bytes;
    }
    return total;
}

std::span<const float> NetStats::BytesInHistory() const
{
    return std::span<const float>(SeriesIn).subspan(kHistory - Closed, Closed);
}

std::span<const float> NetStats::BytesOutHistory() const
{
    return std::span<const float>(SeriesOut).subspan(kHistory - Closed, Closed);
}

void NetStats::Reset()
{
    *this = NetStats{};
}
