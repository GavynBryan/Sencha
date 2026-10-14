#pragma once

#include <perception/Observation.h>

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

// One observer's scan with one sense: its coverage and its observations.
struct SenseScan
{
    EntityId Observer;
    GameplayTagId Sense;
    ScanCoverage Coverage = ScanCoverage::NotEvaluated;
    std::uint32_t First = 0;
    std::uint32_t Count = 0;
};

// This tick's observations, as a World resource, ordered by observer then
// sense. Spans into it are valid until the next Begin; copy what you keep.
class PerceptionFrame
{
public:
    void Begin(std::uint64_t tick);

    // A sense pass reports each observer it scanned between these two calls.
    void BeginScan(EntityId observer, GameplayTagId sense);
    void Add(EntityId subject, const Vec3d& position, float confidence);
    void EndScan(ScanCoverage coverage);

    // After every sense pass has run for the tick.
    void Publish();

    [[nodiscard]] std::uint64_t Tick() const { return CurrentTick; }
    [[nodiscard]] std::span<const SenseScan> Scans() const { return ScanRows; }
    [[nodiscard]] std::span<const SenseScan> ScansOf(EntityId observer) const;
    [[nodiscard]] std::span<const Observation> ObservationsOf(const SenseScan& scan) const;

    // NotEvaluated when the observer was not scanned with that sense.
    [[nodiscard]] ScanCoverage CoverageOf(EntityId observer, GameplayTagId sense) const;
    // Complete only when the observer had at least one scan and every scan it
    // had was Complete.
    [[nodiscard]] ScanCoverage CoverageOf(EntityId observer) const;

    // The observation of `subject` by `observer` with any sense, or null.
    [[nodiscard]] const Observation* Find(EntityId observer, EntityId subject) const;

private:
    static constexpr std::uint32_t kNoOpenScan = std::numeric_limits<std::uint32_t>::max();

    std::uint64_t CurrentTick = 0;
    std::vector<SenseScan> ScanRows;
    std::vector<Observation> Rows;
    std::uint32_t OpenScan = kNoOpenScan;
};
