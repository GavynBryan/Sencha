#pragma once

#include <assets/probes/ProbeVolumeFormat.h>
#include <ecs/StoragePartitionId.h>
#include <ecs/StoragePartitionSet.h>
#include <graphics/GpuFrameRetirement.h>
#include <graphics/GpuImages.h>
#include <render/ProbeVolumeSlotTable.h>
#include <render/RenderLight.h>

#include <cstdint>
#include <memory>
#include <vector>

class LightBindings;
class LoggingProvider;
struct RuntimeZoneRecord;

//=============================================================================
// ProbeVolumeSet
//
// GPU residency for baked irradiance-probe volumes. A zone load hands in its
// decoded .sprobe payload; the set creates three RGBA16F 3D textures per
// volume (one per SH color channel), uploads the fp16 planes verbatim, points
// a lighting-set slot (LightBindings binding 2) at them, and keeps the CPU
// header extraction copies into the frame UBO each frame. Slots are assigned
// at load and stay stable until the owning zone releases them; volumes past
// kMaxActiveProbeVolumes are denied with a warning and render with the
// hemispheric fallback.
//
// Threading: Add/Release run on the owner thread at the async drain point,
// extraction on the same thread later in the frame; there is no concurrent
// access. That ordering is about threads, not about the GPU -- a release at
// the drain point happens while earlier frames are still executing, so slot
// reuse waits on the frame clock (see BeginFrame) rather than on the phase.
// Whatever is still retiring at shutdown is released by the image service's
// own teardown, after the renderer's device wait.
//=============================================================================
class ProbeVolumeSet
{
public:
    void Setup(GpuImages images, std::shared_ptr<LightBindings> bindings,
               LoggingProvider* logging);

    // Advances the frame clock and reclaims volumes whose frames have retired:
    // their descriptors go back to the dummy, their images are handed to the
    // deferred destroy, and their slots become acquirable again.
    //
    // This must be driven from the render phase, not from extraction. A zone
    // unloads at FramePhase::ZoneResidency, several phases before the frame it
    // is unloading in records anything, so the newest frame that can still
    // name the slot is the one that already submitted -- and only the render
    // phase has advanced the clock that far. Refreshing during extraction
    // would stamp releases one frame early, which is a use-after-reuse that
    // only appears at the highest frames-in-flight.
    void BeginFrame(GpuFrameRetirement retirement);

    // Uploads every volume in `file` and assigns binding slots. Re-adding a
    // partition releases its previous volumes first (zone reload). Returns the
    // number of volumes made resident.
    std::size_t AddVolumes(StoragePartitionId partition, const ProbeVolumeFile& file);
    void ReleasePartition(StoragePartitionId partition);
    void ReleaseAll();

    // Appends the resident volume headers of every visible partition to the
    // frame's light set (bounded by the light set's own cap).
    void AppendActive(const StoragePartitionSet& partitions,
                      RenderLightSet& lights) const;

    [[nodiscard]] std::size_t ResidentVolumeCount() const;

    // Released volumes still waiting on the frames that named them. A denial
    // with a nonzero count here is streaming churn -- the slots exist, they
    // are just not proven safe yet -- not a leak.
    [[nodiscard]] std::size_t RetiringVolumeCount() const { return Retiring.size(); }

private:
    struct ResidentVolume
    {
        std::uint32_t Slot = 0;
        ImageHandle Channels[kProbeVolumeChannelCount];
        GpuProbeVolume Header;
    };
    struct PartitionRecord
    {
        StoragePartitionId Partition;
        std::vector<ResidentVolume> Volumes;
    };
    // A volume whose zone is gone, held whole rather than as a bare slot
    // number: binding 2 is not partially bound, so every element must name a
    // live view for as long as the set is bound. Resetting the descriptor and
    // destroying the images therefore happen together, at reclaim, and the
    // images cannot outlive their descriptor or the other way round. That
    // payload is why this is not graphics/RetiredSlotQueue, which gates the
    // same policy over a bare index for the bindless image array.
    struct RetiringVolume
    {
        ResidentVolume Volume;
        std::uint64_t Stamp = 0;
    };

    void ReleaseVolumes(std::vector<ResidentVolume>& volumes);

    GpuImages Images;
    std::shared_ptr<LightBindings> Bindings;
    LoggingProvider* Logging = nullptr;
    std::vector<PartitionRecord> Partitions;
    // FIFO: the oldest release is also the first to retire, so a later entry
    // can never be ready while the front is not.
    std::vector<RetiringVolume> Retiring;
    GpuFrameRetirement Retirement;
    ProbeVolumeSlotTable Slots;
};

//=============================================================================
// ZoneProbeResidency
//
// Zone-record resource tying a zone's probe residency to the record's
// lifetime: zone destruction (streaming unload or shutdown) releases the
// volumes without any explicit hook in the host game.
//=============================================================================
struct ZoneProbeResidency
{
    ZoneProbeResidency(ProbeVolumeSet* set, StoragePartitionId partition)
        : Set(set)
        , Partition(partition)
    {
    }
    ZoneProbeResidency(const ZoneProbeResidency&) = delete;
    ZoneProbeResidency& operator=(const ZoneProbeResidency&) = delete;
    ~ZoneProbeResidency()
    {
        if (Set != nullptr)
            Set->ReleasePartition(Partition);
    }

    ProbeVolumeSet* Set = nullptr;
    StoragePartitionId Partition;
};

// Reads the probe payload cooked beside a scene: for
// `<dir>/<stem>.cooked.json` the cook writes `<dir>/<stem>/probes.sprobe`.
// A missing file is the no-authored-volumes case, not an error (returns
// false with `out` empty). Pure file IO, safe on a zone build task thread.
bool ReadZoneProbeFile(const std::string& cookedScenePath, ProbeVolumeFile& out);

// Makes a decoded payload resident and ties it to the zone record's lifetime
// via a ZoneProbeResidency resource. Owner thread only (descriptor writes).
void AttachZoneProbes(ProbeVolumeSet& set, RuntimeZoneRecord& zone,
                      const ProbeVolumeFile& file);
