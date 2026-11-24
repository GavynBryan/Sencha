#pragma once

#include <core/identity/Id.h>
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

// A pair a load says was occupied, by the persistent ids of its two ends.
struct SensorSeed
{
    PersistentEntityId Sensor;
    PersistentEntityId Occupant;
    friend bool operator==(const SensorSeed&, const SensorSeed&) = default;
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
        std::erase_if(Seeded, predicate);
        ForgetOccurrencesIf(predicate);
    }

    // Leaves the pairs: what is inside stays known until its entities are gone.
    template<class Predicate>
    void ForgetOccurrencesIf(Predicate&& predicate)
    {
        std::erase_if(Observed, [&](const Observation& observation) { return predicate(observation.Pair); });
        std::erase_if(Published, [&](const SensorOccurrence& occurrence) { return predicate(occurrence.Pair); });
    }

    // Seeds wait here until both ends are alive; past the capacity they are refused and counted.
    void Await(std::span<const SensorSeed> seeds);
    [[nodiscard]] std::span<const SensorSeed> Awaited() const { return AwaitedSeeds; }
    template<class Predicate>
    void ForgetAwaitedIf(Predicate&& predicate)
    {
        std::erase_if(AwaitedSeeds, predicate);
    }
    [[nodiscard]] std::uint64_t RefusedSeedCount() const { return RefusedSeeds; }

    // The first sample that finds a seeded pair occupied establishes it without an Enter.
    void Seed(SensorPair pair);
    [[nodiscard]] std::span<const SensorPair> Seeds() const { return Seeded; }
    template<class Predicate>
    void ForgetSeedsIf(Predicate&& predicate)
    {
        std::erase_if(Seeded, predicate);
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
    std::vector<SensorSeed> AwaitedSeeds;
    std::vector<SensorPair> Seeded;
    std::uint64_t Overflows = 0;
    std::uint64_t RefusedSeeds = 0;
    bool Overflow = false;
};
