#include <zone/WorldPartitionIndex.h>

#include <algorithm>

WorldPartitionIndex WorldPartitionIndex::Build(const WorldPartitionManifest& manifest)
{
    WorldPartitionIndex index;

    index.ZoneIds_.reserve(manifest.Zones.size());
    for (const ZoneHeader& zone : manifest.Zones)
        index.ZoneIds_.push_back(zone.Id.Value);
    std::sort(index.ZoneIds_.begin(), index.ZoneIds_.end());
    index.ZoneIds_.erase(std::unique(index.ZoneIds_.begin(), index.ZoneIds_.end()),
                         index.ZoneIds_.end());

    const size_t zoneCount = index.ZoneIds_.size();
    std::vector<std::vector<uint32_t>> outgoing(zoneCount);
    std::vector<std::vector<uint32_t>> incoming(zoneCount);

    for (uint32_t i = 0; i < manifest.Transitions.size(); ++i)
    {
        const TransitionRecord& transition = manifest.Transitions[i];
        const size_t fromSlot = index.FindSlot(transition.From);
        const size_t toSlot = index.FindSlot(transition.To);
        // Dangling edges are skipped: validation reports them and the index
        // stays usable on broken input.
        if (fromSlot == zoneCount || toSlot == zoneCount)
            continue;
        outgoing[fromSlot].push_back(i);
        incoming[toSlot].push_back(i);
    }

    const auto byTransitionId = [&](uint32_t a, uint32_t b)
    {
        const uint64_t idA = manifest.Transitions[a].Id.Value;
        const uint64_t idB = manifest.Transitions[b].Id.Value;
        if (idA != idB)
            return idA < idB;
        return a < b;
    };

    index.OutgoingOffsets_.reserve(zoneCount + 1);
    index.IncomingOffsets_.reserve(zoneCount + 1);

    for (std::vector<uint32_t>& run : outgoing)
    {
        std::sort(run.begin(), run.end(), byTransitionId);
        index.OutgoingOffsets_.push_back(static_cast<uint32_t>(index.Indices_.size()));
        index.Indices_.insert(index.Indices_.end(), run.begin(), run.end());
    }
    index.OutgoingOffsets_.push_back(static_cast<uint32_t>(index.Indices_.size()));

    for (std::vector<uint32_t>& run : incoming)
    {
        std::sort(run.begin(), run.end(), byTransitionId);
        index.IncomingOffsets_.push_back(static_cast<uint32_t>(index.Indices_.size()));
        index.Indices_.insert(index.Indices_.end(), run.begin(), run.end());
    }
    index.IncomingOffsets_.push_back(static_cast<uint32_t>(index.Indices_.size()));

    index.DockOffsets_.reserve(zoneCount + 1);
    index.LinkOffsets_.reserve(zoneCount + 1);
    for (uint64_t zoneValue : index.ZoneIds_)
    {
        index.DockOffsets_.push_back(static_cast<uint32_t>(index.Docks_.size()));
        index.LinkOffsets_.push_back(static_cast<uint32_t>(index.Links_.size()));
        for (const ZoneHeader& zone : manifest.Zones)
        {
            if (zone.Id.Value != zoneValue)
                continue;
            index.Docks_.insert(index.Docks_.end(), zone.Docks.begin(), zone.Docks.end());
            index.Links_.insert(index.Links_.end(), zone.Links.begin(), zone.Links.end());
            break;
        }
        std::sort(index.Docks_.begin() + index.DockOffsets_.back(), index.Docks_.end(),
                  [](const DockEndpoint& a, const DockEndpoint& b)
                  {
                      if (a.Id.Value != b.Id.Value)
                          return a.Id.Value < b.Id.Value;
                      return a.Side < b.Side;
                  });
        std::sort(index.Links_.begin() + index.LinkOffsets_.back(), index.Links_.end(),
                  [](const LinkEndpoint& a, const LinkEndpoint& b)
                  {
                      if (a.Id.Value != b.Id.Value)
                          return a.Id.Value < b.Id.Value;
                      return a.Side < b.Side;
                  });
    }
    index.DockOffsets_.push_back(static_cast<uint32_t>(index.Docks_.size()));
    index.LinkOffsets_.push_back(static_cast<uint32_t>(index.Links_.size()));

    return index;
}

size_t WorldPartitionIndex::FindSlot(ZoneId zone) const
{
    const auto it = std::lower_bound(ZoneIds_.begin(), ZoneIds_.end(), zone.Value);
    if (it == ZoneIds_.end() || *it != zone.Value)
        return ZoneIds_.size();
    return static_cast<size_t>(it - ZoneIds_.begin());
}

std::span<const uint32_t> WorldPartitionIndex::Outgoing(ZoneId zone) const
{
    const size_t slot = FindSlot(zone);
    if (slot == ZoneIds_.size())
        return {};
    return { Indices_.data() + OutgoingOffsets_[slot],
             Indices_.data() + OutgoingOffsets_[slot + 1] };
}

std::span<const uint32_t> WorldPartitionIndex::Incoming(ZoneId zone) const
{
    const size_t slot = FindSlot(zone);
    if (slot == ZoneIds_.size())
        return {};
    return { Indices_.data() + IncomingOffsets_[slot],
             Indices_.data() + IncomingOffsets_[slot + 1] };
}

std::span<const DockEndpoint> WorldPartitionIndex::DocksFrom(ZoneId zone) const
{
    const size_t slot = FindSlot(zone);
    if (slot == ZoneIds_.size())
        return {};
    return { Docks_.data() + DockOffsets_[slot],
             Docks_.data() + DockOffsets_[slot + 1] };
}

std::span<const LinkEndpoint> WorldPartitionIndex::LinksFrom(ZoneId zone) const
{
    const size_t slot = FindSlot(zone);
    if (slot == ZoneIds_.size())
        return {};
    return { Links_.data() + LinkOffsets_[slot],
             Links_.data() + LinkOffsets_[slot + 1] };
}

bool WorldPartitionIndex::ContainsZone(ZoneId zone) const
{
    return FindSlot(zone) != ZoneIds_.size();
}
