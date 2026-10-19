#pragma once

#include <ecs/EntityId.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

struct SensorQueryCounts
{
    std::uint64_t Overlaps = 0;
    std::uint64_t Casts = 0;
};

struct SensorPair
{
    EntityId Sensor;
    EntityId Activator;
    auto operator<=>(const SensorPair&) const = default;
};

enum class SensorTransition : std::uint8_t { Enter, Stay, Exit };

struct SensorOccurrence
{
    SensorPair Pair;
    SensorTransition Transition;
    std::uint64_t Tick = 0;
};

// Owner-thread observations; Commit publishes one complete, sorted physics sample.
class SensorOverlaps
{
public:
    explicit SensorOverlaps(std::size_t capacity = 4096);

    void Begin();
    void RecordQueries(bool cast) { ++Queries.Overlaps; Queries.Casts += cast ? 1 : 0; }
    [[nodiscard]] SensorQueryCounts QueryCounts() const { return Queries; }
    [[nodiscard]] bool Observe(SensorPair pair, bool occupied);
    [[nodiscard]] bool Commit(std::uint64_t tick);

    template<class Predicate>
    void ForgetIf(Predicate&& predicate)
    {
        std::erase_if(Active, predicate);
        std::erase_if(Observed, [&](const Observation& observation) { return predicate(observation.Pair); });
        std::erase_if(Published, [&](const SensorOccurrence& occurrence) { return predicate(occurrence.Pair); });
    }

    [[nodiscard]] std::span<const SensorOccurrence> Occurrences() const { return Published; }
    [[nodiscard]] std::span<const SensorPair> Pairs() const { return Active; }
    [[nodiscard]] std::size_t Capacity() const { return Limit; }
    [[nodiscard]] std::uint64_t OverflowCount() const { return Overflows; }

private:
    struct Observation
    {
        SensorPair Pair;
        bool Occupied;
    };

    SensorQueryCounts Queries;
    std::size_t Limit;
    std::vector<SensorPair> Active;
    std::vector<SensorPair> Next;
    std::vector<Observation> Observed;
    std::vector<SensorOccurrence> Published;
    std::uint64_t Overflows = 0;
    bool Overflow = false;
};
