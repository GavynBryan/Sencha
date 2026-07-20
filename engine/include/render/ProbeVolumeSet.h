#pragma once

#include <assets/probes/ProbeVolumeFormat.h>
#include <render/LightBindings.h>
#include <render/RenderLight.h>
#include <world/registry/RegistryId.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

class LoggingProvider;
class VulkanImageService;
struct Registry;

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
// access. GPU teardown ordering rides zone destruction: registries die before
// graphics services, and image destruction defers through the deletion queue.
//=============================================================================
class ProbeVolumeSet
{
public:
    void Setup(VulkanImageService* images, std::shared_ptr<LightBindings> bindings,
               LoggingProvider* logging);

    // Uploads every volume in `file` and assigns binding slots. Re-adding a
    // registry releases its previous volumes first (zone reload). Returns the
    // number of volumes made resident.
    std::size_t AddZoneVolumes(RegistryId registry, const ProbeVolumeFile& file);
    void ReleaseZone(RegistryId registry);
    void ReleaseAll();

    // Appends the resident volume headers of every listed registry to the
    // frame's light set (bounded by the light set's own cap).
    void AppendActive(std::span<Registry* const> registries,
                      RenderLightSet& lights) const;

    [[nodiscard]] std::size_t ResidentVolumeCount() const;

private:
    struct ResidentVolume
    {
        std::uint32_t Slot = 0;
        ImageHandle Channels[kProbeVolumeChannelCount];
        GpuProbeVolume Header;
    };
    struct ZoneRecord
    {
        RegistryId Registry;
        std::vector<ResidentVolume> Volumes;
    };

    void ReleaseVolumes(std::vector<ResidentVolume>& volumes);

    VulkanImageService* Images = nullptr;
    std::shared_ptr<LightBindings> Bindings;
    LoggingProvider* Logging = nullptr;
    std::vector<ZoneRecord> Zones;
    bool SlotUsed[kMaxActiveProbeVolumes] = {};
};

//=============================================================================
// ZoneProbeResidency
//
// Registry resource tying a zone's probe residency to the registry's
// lifetime: zone destruction (streaming unload or shutdown) releases the
// volumes without any explicit hook in the host game.
//=============================================================================
struct ZoneProbeResidency
{
    ZoneProbeResidency(ProbeVolumeSet* set, RegistryId registry)
        : Set(set)
        , Registry(registry)
    {
    }
    ZoneProbeResidency(const ZoneProbeResidency&) = delete;
    ZoneProbeResidency& operator=(const ZoneProbeResidency&) = delete;
    ~ZoneProbeResidency()
    {
        if (Set != nullptr)
            Set->ReleaseZone(Registry);
    }

    ProbeVolumeSet* Set = nullptr;
    RegistryId Registry;
};

// Reads the probe payload cooked beside a scene: for
// `<dir>/<stem>.cooked.json` the cook writes `<dir>/<stem>/probes.sprobe`.
// A missing file is the no-authored-volumes case, not an error (returns
// false with `out` empty). Pure file IO, safe on a zone build task thread.
bool ReadZoneProbeFile(const std::string& cookedScenePath, ProbeVolumeFile& out);

// Makes a decoded payload resident and ties it to the registry's lifetime via
// a ZoneProbeResidency resource. Owner thread only (descriptor writes).
void AttachZoneProbes(ProbeVolumeSet& set, Registry& registry,
                      const ProbeVolumeFile& file);
