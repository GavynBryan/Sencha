#include <render/ProbeVolumeSet.h>

#include <core/logging/LoggingProvider.h>
#include <render/pass/LightBindings.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <fstream>
#include <string_view>

void ProbeVolumeSet::Setup(GpuImages images,
                           std::shared_ptr<LightBindings> bindings,
                           LoggingProvider* logging)
{
    Images = images;
    Bindings = std::move(bindings);
    Logging = logging;
}

std::size_t ProbeVolumeSet::AddVolumes(StoragePartitionId partition,
                                       const ProbeVolumeFile& file)
{
    if (!Images.IsValid() || Bindings == nullptr)
        return 0;
    ReleasePartition(partition);

    PartitionRecord record;
    record.Partition = partition;
    for (const ProbeVolumeRecord& volume : file.Volumes)
    {
        const std::uint32_t count = volume.Grid.PointCount();
        const std::size_t planeHalves =
            static_cast<std::size_t>(count) * kProbeShCoefficients;
        if (count == 0
            || volume.ShHalf.size() != planeHalves * kProbeShChannels)
        {
            if (Logging != nullptr)
                Logging->GetLogger<ProbeVolumeSet>().Warn(
                    "probe volume {}: malformed SH payload, skipped",
                    volume.StableIndex);
            continue;
        }

        const std::uint32_t slot = Slots.Acquire();
        if (slot == ProbeVolumeSlotTable::kInvalidSlot)
        {
            // Retiring slots are counted so churn reads differently from a
            // full set: a zone reload releases and re-acquires in one call, so
            // it transiently needs both copies' slots and can be denied for
            // the frames its predecessor takes to retire.
            if (Logging != nullptr)
                Logging->GetLogger<ProbeVolumeSet>().Warn(
                    "probe volume {}: no free slot of {} ({} retiring), volume "
                    "denied (hemispheric fallback)",
                    volume.StableIndex, kMaxActiveProbeVolumes,
                    Slots.CountIn(ProbeVolumeSlotTable::State::Retiring));
            continue;
        }

        ResidentVolume resident;
        resident.Slot = slot;
        bool uploaded = true;
        for (std::uint32_t channel = 0; channel < kProbeVolumeChannelCount;
             ++channel)
        {
            resident.Channels[channel] = Images.Create(ImageDesc{
                .Format = GpuFormat::Rgba16Float,
                .Extent = { volume.Grid.DimsX, volume.Grid.DimsY },
                .Depth = volume.Grid.DimsZ,
                .ViewKind = GpuImageViewKind::Volume,
                .DebugName = "Probe volume SH channel",
            });
            if (!resident.Channels[channel].IsValid()
                || !Images.Upload(
                    resident.Channels[channel],
                    volume.ShHalf.data() + channel * planeHalves,
                    planeHalves * sizeof(std::uint16_t)))
            {
                uploaded = false;
                break;
            }
        }
        if (!uploaded)
        {
            for (const ImageHandle& channel : resident.Channels)
                if (channel.IsValid())
                    Images.Destroy(channel);
            // The slot was taken before the upload was attempted, and nothing
            // has been bound to it, so hand it straight back rather than
            // stranding it for the lifetime of the set.
            Slots.Release(slot);
            if (Logging != nullptr)
                Logging->GetLogger<ProbeVolumeSet>().Warn(
                    "probe volume {}: GPU upload failed, volume dropped",
                    volume.StableIndex);
            continue;
        }

        Bindings->SetProbeVolume(slot, resident.Channels[0],
                                 resident.Channels[1], resident.Channels[2]);
        resident.Header = MakeGpuProbeVolume(volume.Grid, volume.Priority,
                                             volume.StableIndex, slot);
        record.Volumes.push_back(resident);
    }

    const std::size_t added = record.Volumes.size();
    if (added > 0)
    {
        if (Logging != nullptr)
            Logging->GetLogger<ProbeVolumeSet>().Info(
                "probe volumes resident: {} added ({} total)", added,
                ResidentVolumeCount() + added);
        Partitions.push_back(std::move(record));
    }
    return added;
}

void ProbeVolumeSet::ReleasePartition(StoragePartitionId partition)
{
    const auto it = std::find_if(Partitions.begin(), Partitions.end(),
                                 [&](const PartitionRecord& record)
                                 { return record.Partition == partition; });
    if (it == Partitions.end())
        return;
    ReleaseVolumes(it->Volumes);
    Partitions.erase(it);
}

void ProbeVolumeSet::ReleaseAll()
{
    for (PartitionRecord& record : Partitions)
        ReleaseVolumes(record.Volumes);
    Partitions.clear();
}

// Nothing is torn down here. Deferring the image destroy protects the memory
// but says nothing about the identity: the slot number is what an in-flight
// frame's uniform header carries, and the shader resolves it into binding 2 at
// execution time. Handing the slot straight back would let a frame sample the
// next zone's volume through this zone's world-to-uvw mapping, and rewriting
// the descriptor to the dummy would mutate an element that frame is still
// dynamically using, which update-after-bind does not permit.
void ProbeVolumeSet::ReleaseVolumes(std::vector<ResidentVolume>& volumes)
{
    for (const ResidentVolume& volume : volumes)
    {
        Slots.BeginRetire(volume.Slot);
        Retiring.push_back(RetiringVolume{ volume, Retirement.Stamp() });
    }
    volumes.clear();
}

void ProbeVolumeSet::BeginFrame(GpuFrameRetirement retirement)
{
    Retirement = retirement;
    std::size_t reclaimed = 0;
    while (!Retiring.empty() && Retirement.IsRetired(Retiring.front().Stamp))
    {
        const ResidentVolume& volume = Retiring.front().Volume;
        // Descriptor first, then the images it named: binding 2 has no
        // partially-bound bit, so the element must point at the dummy before
        // its views become destroyable.
        if (Bindings != nullptr)
            Bindings->ResetProbeVolume(volume.Slot);
        if (Images.IsValid())
            for (const ImageHandle& channel : volume.Channels)
                if (channel.IsValid())
                    Images.Destroy(channel);
        Slots.Release(volume.Slot);
        Retiring.erase(Retiring.begin());
        ++reclaimed;
    }
    // Symmetric with the residency line in AddVolumes, and the only way a live
    // run can tell a working gate from a stalled one: slots that never come
    // back deny every zone that streams in behind them.
    if (reclaimed > 0 && Logging != nullptr)
        Logging->GetLogger<ProbeVolumeSet>().Info(
            "probe volume slots reclaimed: {} ({} still retiring)", reclaimed,
            Retiring.size());
}

void ProbeVolumeSet::AppendActive(const StoragePartitionSet& partitions,
                                  RenderLightSet& lights) const
{
    for (const PartitionRecord& record : Partitions)
    {
        if (!partitions.Contains(record.Partition))
            continue;
        for (const ResidentVolume& volume : record.Volumes)
            (void)lights.AddProbeVolume(volume.Header);
    }
}

std::size_t ProbeVolumeSet::ResidentVolumeCount() const
{
    std::size_t count = 0;
    for (const PartitionRecord& record : Partitions)
        count += record.Volumes.size();
    return count;
}

bool ReadZoneProbeFile(const std::string& cookedScenePath, ProbeVolumeFile& out)
{
    out.Volumes.clear();
    constexpr std::string_view cookedSuffix = ".smap";
    if (!cookedScenePath.ends_with(cookedSuffix))
        return false;
    std::string probePath =
        cookedScenePath.substr(0, cookedScenePath.size() - cookedSuffix.size());
    probePath += "/probes.sprobe";

    std::ifstream stream(probePath, std::ios::binary);
    if (!stream.is_open())
        return false;
    BinaryReader reader(stream);
    if (!ReadProbeVolumeFile(reader, out))
    {
        out.Volumes.clear();
        return false;
    }
    return !out.Volumes.empty();
}

void AttachZoneProbes(ProbeVolumeSet& set, RuntimeZoneRecord& zone,
                      const ProbeVolumeFile& file)
{
    if (file.Volumes.empty())
        return;
    if (set.AddVolumes(zone.Partition, file) > 0)
        zone.Resources.Register<ZoneProbeResidency>(&set, zone.Partition);
}
