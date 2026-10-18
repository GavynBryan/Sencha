#include <physics/SensorOverlaps.h>

SensorOverlaps::SensorOverlaps(std::size_t capacity)
    : Limit(std::max<std::size_t>(capacity, 1))
{
    Active.reserve(Limit);
    Next.reserve(Limit);
    Observed.reserve(Limit);
    Published.reserve(3 * Limit);
}

void SensorOverlaps::Begin()
{
    Observed.clear();
    Published.clear();
    Overflow = false;
}

bool SensorOverlaps::Observe(SensorPair pair, bool occupied)
{
    if (!pair.Sensor.IsValid() || !pair.Activator.IsValid() || pair.Sensor == pair.Activator)
        return true;
    if (!Observed.empty() && Observed.back().Pair == pair)
    {
        Observed.back().Occupied |= occupied;
        return true;
    }
    if (Observed.size() == Limit)
    {
        if (!Overflow)
            ++Overflows;
        Overflow = true;
        return false;
    }
    Observed.push_back(Observation{ pair, occupied });
    return true;
}

bool SensorOverlaps::Commit(std::uint64_t tick)
{
    if (Overflow)
        return false;
    std::sort(Observed.begin(), Observed.end(),
        [](const Observation& a, const Observation& b) { return a.Pair < b.Pair; });
    std::size_t unique = 0;
    for (const Observation& observation : Observed)
    {
        if (unique != 0 && Observed[unique - 1].Pair == observation.Pair)
            Observed[unique - 1].Occupied |= observation.Occupied;
        else
            Observed[unique++] = observation;
    }
    Observed.resize(unique);
    Next.clear();
    std::size_t previous = 0;
    for (const Observation& observation : Observed)
    {
        while (previous < Active.size() && Active[previous] < observation.Pair)
            Published.push_back({ Active[previous++], SensorTransition::Exit, tick });
        const bool wasInside = previous < Active.size() && Active[previous] == observation.Pair;
        if (wasInside)
            ++previous;
        if (observation.Occupied)
        {
            Next.push_back(observation.Pair);
            Published.push_back({ observation.Pair, wasInside ? SensorTransition::Stay : SensorTransition::Enter, tick });
        }
        else
        {
            if (!wasInside)
                Published.push_back({ observation.Pair, SensorTransition::Enter, tick });
            Published.push_back({ observation.Pair, SensorTransition::Exit, tick });
        }
    }
    while (previous < Active.size())
        Published.push_back({ Active[previous++], SensorTransition::Exit, tick });
    Active.swap(Next);
    return true;
}
