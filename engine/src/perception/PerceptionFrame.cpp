#include <perception/PerceptionFrame.h>

#include <algorithm>
#include <cassert>
#include <tuple>

namespace
{
    [[nodiscard]] bool ObserverBefore(const SenseScan& scan, EntityId observer) { return scan.Observer < observer; }
    [[nodiscard]] bool ObserverAfter(EntityId observer, const SenseScan& scan) { return observer < scan.Observer; }
}

void PerceptionFrame::Begin(std::uint64_t tick)
{
    assert(OpenScan == kNoOpenScan);
    CurrentTick = tick;
    ScanRows.clear();
    Rows.clear();
}

void PerceptionFrame::BeginScan(EntityId observer, GameplayTagId sense)
{
    assert(OpenScan == kNoOpenScan);
    OpenScan = static_cast<std::uint32_t>(ScanRows.size());
    ScanRows.push_back(SenseScan{ .Observer = observer,
                                  .Sense = sense,
                                  .First = static_cast<std::uint32_t>(Rows.size()) });
}

void PerceptionFrame::Add(EntityId subject, const Vec3d& position, float confidence)
{
    assert(OpenScan != kNoOpenScan);
    SenseScan& scan = ScanRows[OpenScan];
    Rows.push_back(Observation{ .Observer = scan.Observer,
                                .Sense = scan.Sense,
                                .Subject = subject,
                                .Position = position,
                                .Tick = CurrentTick,
                                .Confidence = std::clamp(confidence, 0.0f, 1.0f) });
    ++scan.Count;
}

void PerceptionFrame::EndScan(ScanCoverage coverage)
{
    assert(OpenScan != kNoOpenScan);
    ScanRows[OpenScan].Coverage = coverage;
    OpenScan = kNoOpenScan;
}

void PerceptionFrame::Publish()
{
    assert(OpenScan == kNoOpenScan);
    std::ranges::sort(ScanRows, [](const SenseScan& left, const SenseScan& right) {
        return std::tie(left.Observer, left.Sense) < std::tie(right.Observer, right.Sense);
    });
}

std::span<const SenseScan> PerceptionFrame::ScansOf(EntityId observer) const
{
    const auto first = std::lower_bound(ScanRows.begin(), ScanRows.end(), observer, ObserverBefore);
    const auto last = std::upper_bound(first, ScanRows.end(), observer, ObserverAfter);
    return { first, last };
}

std::span<const Observation> PerceptionFrame::ObservationsOf(const SenseScan& scan) const
{
    return std::span<const Observation>(Rows).subspan(scan.First, scan.Count);
}

const SenseScan* PerceptionFrame::ScanOf(EntityId observer, GameplayTagId sense) const
{
    for (const SenseScan& scan : ScansOf(observer))
        if (scan.Sense == sense)
            return &scan;
    return nullptr;
}

ScanCoverage PerceptionFrame::CoverageOf(EntityId observer, GameplayTagId sense) const
{
    const SenseScan* scan = ScanOf(observer, sense);
    return scan != nullptr ? scan->Coverage : ScanCoverage::NotEvaluated;
}

ScanCoverage PerceptionFrame::CoverageOf(EntityId observer) const
{
    const std::span<const SenseScan> scans = ScansOf(observer);
    if (scans.empty())
        return ScanCoverage::NotEvaluated;
    for (const SenseScan& scan : scans)
        if (scan.Coverage != ScanCoverage::Complete)
            return ScanCoverage::Partial;
    return ScanCoverage::Complete;
}

const Observation* PerceptionFrame::Find(EntityId observer, GameplayTagId sense, EntityId subject) const
{
    const SenseScan* scan = ScanOf(observer, sense);
    if (scan == nullptr)
        return nullptr;
    for (const Observation& observation : ObservationsOf(*scan))
        if (observation.Subject == subject)
            return &observation;
    return nullptr;
}
