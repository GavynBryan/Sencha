#include <render/ProbeVolumeSet.h>

#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <fstream>
#include <string_view>

void ProbeVolumeSet::Setup(VulkanImageService* images,
                           std::shared_ptr<LightBindings> bindings,
                           LoggingProvider* logging)
{
    Images = images;
    Bindings = std::move(bindings);
    Logging = logging;
}

std::size_t ProbeVolumeSet::AddZoneVolumes(StoragePartitionId zone,
                                           const ProbeVolumeFile& file)
{
    if (Images == nullptr || Bindings == nullptr)
        return 0;
    ReleaseZone(zone);

    ZoneRecord record;
    record.Partition = zone;
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

        std::uint32_t slot = kMaxActiveProbeVolumes;
        for (std::uint32_t candidate = 0; candidate < kMaxActiveProbeVolumes;
             ++candidate)
        {
            if (!SlotUsed[candidate])
            {
                slot = candidate;
                break;
            }
        }
        if (slot == kMaxActiveProbeVolumes)
        {
            if (Logging != nullptr)
                Logging->GetLogger<ProbeVolumeSet>().Warn(
                    "probe volume {}: all {} slots resident, volume denied "
                    "(hemispheric fallback)",
                    volume.StableIndex, kMaxActiveProbeVolumes);
            continue;
        }

        ResidentVolume resident;
        resident.Slot = slot;
        bool uploaded = true;
        for (std::uint32_t channel = 0; channel < kProbeVolumeChannelCount;
             ++channel)
        {
            resident.Channels[channel] = Images->Create(ImageCreateInfo{
                .Format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .Extent = { volume.Grid.DimsX, volume.Grid.DimsY },
                .Usage = VK_IMAGE_USAGE_SAMPLED_BIT
                       | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .ViewType = VK_IMAGE_VIEW_TYPE_3D,
                .Depth = volume.Grid.DimsZ,
                .DebugName = "Probe volume SH channel",
            });
            if (!resident.Channels[channel].IsValid()
                || !Images->Upload(
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
                    Images->Destroy(channel);
            if (Logging != nullptr)
                Logging->GetLogger<ProbeVolumeSet>().Warn(
                    "probe volume {}: GPU upload failed, volume dropped",
                    volume.StableIndex);
            continue;
        }

        Bindings->SetProbeVolume(slot,
                                 Images->GetView(resident.Channels[0]),
                                 Images->GetView(resident.Channels[1]),
                                 Images->GetView(resident.Channels[2]));
        resident.Header = MakeGpuProbeVolume(volume.Grid, volume.Priority,
                                             volume.StableIndex, slot);
        SlotUsed[slot] = true;
        record.Volumes.push_back(resident);
    }

    const std::size_t added = record.Volumes.size();
    if (added > 0)
    {
        if (Logging != nullptr)
            Logging->GetLogger<ProbeVolumeSet>().Info(
                "probe volumes resident: {} added ({} total)", added,
                ResidentVolumeCount() + added);
        Zones.push_back(std::move(record));
    }
    return added;
}

void ProbeVolumeSet::ReleaseZone(StoragePartitionId zone)
{
    const auto it = std::find_if(Zones.begin(), Zones.end(),
                                 [&](const ZoneRecord& record)
                                 { return record.Partition == zone; });
    if (it == Zones.end())
        return;
    ReleaseVolumes(it->Volumes);
    Zones.erase(it);
}

void ProbeVolumeSet::ReleaseAll()
{
    for (ZoneRecord& zone : Zones)
        ReleaseVolumes(zone.Volumes);
    Zones.clear();
}

void ProbeVolumeSet::ReleaseVolumes(std::vector<ResidentVolume>& volumes)
{
    for (ResidentVolume& volume : volumes)
    {
        if (Bindings != nullptr)
            Bindings->ResetProbeVolume(volume.Slot);
        if (Images != nullptr)
            for (const ImageHandle& channel : volume.Channels)
                if (channel.IsValid())
                    Images->Destroy(channel);
        SlotUsed[volume.Slot] = false;
    }
    volumes.clear();
}

void ProbeVolumeSet::AppendActive(const StoragePartitionSet& partitions,
                                  RenderLightSet& lights) const
{
    for (const ZoneRecord& zone : Zones)
    {
        if (!partitions.Contains(zone.Partition))
            continue;
        for (const ResidentVolume& volume : zone.Volumes)
            (void)lights.AddProbeVolume(volume.Header);
    }
}

std::size_t ProbeVolumeSet::ResidentVolumeCount() const
{
    std::size_t count = 0;
    for (const ZoneRecord& zone : Zones)
        count += zone.Volumes.size();
    return count;
}

bool ReadZoneProbeFile(const std::string& cookedScenePath, ProbeVolumeFile& out)
{
    out.Volumes.clear();
    constexpr std::string_view cookedSuffix = ".cooked.json";
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
    if (set.AddZoneVolumes(zone.Partition, file) > 0)
        zone.Resources.Register<ZoneProbeResidency>(&set, zone.Partition);
}
