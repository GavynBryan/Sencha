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

    // Detached-build workflow for async zone loading (docs/ecs/parallelization.md,
    // Decision 3): reserve an id on the main thread, build a Registry off-thread
    // with MakeZoneRegistry(reservedId, zone), then attach it here between
    // frames. Attach is main-thread-only, like all zone lifecycle.
    RegistryId ReserveRegistryId();
    Registry& AttachZone(std::unique_ptr<Registry> registry,
                         ZoneParticipation participation = {});

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
            fn(loaded->Zone, *loaded->ZoneRegistry, loaded->Participation);
        }
    }

    // The returned view is valid until EndFrameView. Zone lifecycle
    // (CreateZone, AttachZone, DestroyZone) asserts that no view is live:
    // mutations belong at the drain point, before the frame's view is built.
    // Spans therefore never contain null entries.
    FrameRegistryView BuildFrameView();

    // Clears entity storage without destroying registry identity or participation.
    // Shutdown uses this while game and engine resources still outlive component
    // removal hooks.
    void ClearEntities();

    // Advances the ECS change epoch of the global registry and every loaded
    // zone exactly once. Dormant zones advance too: participation determines
    // which systems run, not the registry's change-detection timeline.
    void AdvanceFrameEpochs();

    void EndFrameView();

private:
    struct LoadedZone
    {
        ZoneId Zone;
        std::unique_ptr<Registry> ZoneRegistry;
        ZoneParticipation Participation;
    };

    LoadedZone* FindLoadedZone(ZoneId zone);
    const LoadedZone* FindLoadedZone(ZoneId zone) const;

    RegistryId AllocateRegistryId();
    void InvalidateFrameScratch();

    // True between BuildFrameView and EndFrameView: the window in which zone
    // lifecycle mutation would dangle span entries mid-frame.
    bool FrameViewLive_ = false;

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
