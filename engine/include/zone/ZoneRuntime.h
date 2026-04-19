#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <world/registry/FrameRegistryView.h>
#include <world/registry/Registry.h>
#include <vector>
#include <zone/ZoneId.h>
#include <zone/ZoneParticipation.h>

class ZoneRuntime
{
public:
    ZoneRuntime();
    ~ZoneRuntime();

    ZoneRuntime(const ZoneRuntime&) = delete;
    ZoneRuntime& operator=(const ZoneRuntime&) = delete;
    ZoneRuntime(ZoneRuntime&&) = delete;
    ZoneRuntime& operator=(ZoneRuntime&&) = delete;

    Registry& Global();
    const Registry& Global() const;

    Registry& CreateZone(ZoneId zone);
    bool DestroyZone(ZoneId zone);
    bool IsZoneLoaded(ZoneId zone) const;

    Registry* FindZone(ZoneId zone);
    const Registry* FindZone(ZoneId zone) const;

    Registry* FindRegistry(RegistryId id);
    const Registry* FindRegistry(RegistryId id) const;

    ZoneParticipation GetParticipation(ZoneId zone) const;
    void SetParticipation(ZoneId zone, ZoneParticipation participation);

    std::size_t ZoneCount() const;

    // Intended for debug/tools/tests. Runtime systems should consume
    // FrameRegistryView instead of walking ZoneRuntime directly.
    template<typename Fn>
    void VisitZones(Fn&& fn) const
    {
        for (const auto& loaded : Zones)
        {
            fn(loaded->Zone, *loaded->Registry, loaded->Participation);
        }
    }

    // The returned view is valid only until the next BuildFrameView call or any
    // zone lifecycle mutation on this ZoneRuntime. Do not mutate zones while a
    // frame is executing.
    FrameRegistryView BuildFrameView();

private:
    struct LoadedZone
    {
        ZoneId Zone;
        std::unique_ptr<Registry> Registry;
        ZoneParticipation Participation;
    };

    LoadedZone* FindLoadedZone(ZoneId zone);
    const LoadedZone* FindLoadedZone(ZoneId zone) const;

    RegistryId AllocateRegistryId();
    void InvalidateFrameScratch();

    std::unique_ptr<Registry> GlobalRegistry;
    std::vector<std::unique_ptr<LoadedZone>> Zones;

    // Reusable temporary storage for BuildFrameView().
    // These buffers own the arrays referenced by FrameRegistryView spans.
    std::vector<Registry*> VisibleScratch;
    std::vector<Registry*> PhysicsScratch;
    std::vector<Registry*> LogicScratch;
    std::vector<Registry*> AudioScratch;

    std::uint16_t NextRegistryIndex = 2;
};
