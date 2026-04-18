#include <zone/ZoneRuntime.h>

#include <cassert>
#include <algorithm>
#include <limits>
#include <span>

ZoneRuntime::ZoneRuntime()
    : Global(std::make_unique<Registry>(MakeGlobalRegistry(RegistryId::Global())))
{
}

ZoneRuntime::~ZoneRuntime() = default;

Registry& ZoneRuntime::Global()
{
    return *Global;
}

const Registry& ZoneRuntime::Global() const
{
    return *Global;
}

Registry& ZoneRuntime::CreateZone(ZoneId zone)
{
    assert(zone.IsValid() && "ZoneRuntime::CreateZone: zone id must be valid");
    assert(!FindLoadedZone(zone) && "ZoneRuntime::CreateZone: duplicate zone id");

    InvalidateFrameScratch();

    const RegistryId registryId = AllocateRegistryId();
    auto registry = std::make_unique<Registry>(MakeZoneRegistry(registryId, zone));

    auto loaded = std::make_unique<LoadedZone>();
    loaded->Zone = zone;
    loaded->Registry = std::move(registry);
    loaded->Participation = {};

    assert(loaded->Zone == loaded->Registry->Zone && "LoadedZone and Registry ZoneIds must match");

    Registry* registryPtr = loaded->Registry.get();
    Zones.push_back(std::move(loaded));
    return *registryPtr;
}

bool ZoneRuntime::DestroyZone(ZoneId zone)
{
    auto it = std::find_if(Zones.begin(), Zones.end(),
        [zone](const std::unique_ptr<LoadedZone>& loaded) {
            return loaded->Zone == zone;
        });

    if (it == Zones.end())
        return false;

    InvalidateFrameScratch();
    Zones.erase(it);
    return true;
}

bool ZoneRuntime::IsZoneLoaded(ZoneId zone) const
{
    return FindLoadedZone(zone) != nullptr;
}

Registry* ZoneRuntime::FindZone(ZoneId zone)
{
    LoadedZone* loaded = FindLoadedZone(zone);
    return loaded ? loaded->Registry.get() : nullptr;
}

const Registry* ZoneRuntime::FindZone(ZoneId zone) const
{
    const LoadedZone* loaded = FindLoadedZone(zone);
    return loaded ? loaded->Registry.get() : nullptr;
}

Registry* ZoneRuntime::FindRegistry(RegistryId id)
{
    if (!id.IsValid())
        return nullptr;

    if (Global && Global->Id == id)
        return Global.get();

    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->Registry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (loaded->Registry->Id == id)
            return loaded->Registry.get();
    }

    return nullptr;
}

const Registry* ZoneRuntime::FindRegistry(RegistryId id) const
{
    if (!id.IsValid())
        return nullptr;

    if (Global && Global->Id == id)
        return Global.get();

    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->Registry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (loaded->Registry->Id == id)
            return loaded->Registry.get();
    }

    return nullptr;
}

ZoneParticipation ZoneRuntime::GetParticipation(ZoneId zone) const
{
    const LoadedZone* loaded = FindLoadedZone(zone);
    assert(loaded && "ZoneRuntime::GetParticipation: zone must be loaded");
    return loaded->Participation;
}

void ZoneRuntime::SetParticipation(ZoneId zone, ZoneParticipation participation)
{
    LoadedZone* loaded = FindLoadedZone(zone);
    assert(loaded && "ZoneRuntime::SetParticipation: zone must be loaded");
    loaded->Participation = participation;
}

std::size_t ZoneRuntime::ZoneCount() const
{
    return Zones.size();
}

FrameRegistryView ZoneRuntime::BuildFrameView()
{
    InvalidateFrameScratch();

    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->Registry->Zone && "LoadedZone and Registry ZoneIds must match");

        Registry* registry = loaded->Registry.get();
        const ZoneParticipation& participation = loaded->Participation;

        if (participation.Visible)
            VisibleScratch.push_back(registry);
        if (participation.Physics)
            PhysicsScratch.push_back(registry);
        if (participation.Logic)
            LogicScratch.push_back(registry);
        if (participation.Audio)
            AudioScratch.push_back(registry);
    }

    return FrameRegistryView{
        .Global = Global_.get(),
        .Visible = std::span<Registry*>{ VisibleScratch_.data(), VisibleScratch_.size() },
        .Physics = std::span<Registry*>{ PhysicsScratch_.data(), PhysicsScratch_.size() },
        .Logic = std::span<Registry*>{ LogicScratch_.data(), LogicScratch_.size() },
        .Audio = std::span<Registry*>{ AudioScratch_.data(), AudioScratch_.size() }
    };
}

ZoneRuntime::LoadedZone* ZoneRuntime::FindLoadedZone(ZoneId zone)
{
    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->Registry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (loaded->Zone == zone)
            return loaded.get();
    }

    return nullptr;
}

const ZoneRuntime::LoadedZone* ZoneRuntime::FindLoadedZone(ZoneId zone) const
{
    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->Registry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (loaded->Zone == zone)
            return loaded.get();
    }

    return nullptr;
}

RegistryId ZoneRuntime::AllocateRegistryId()
{
    assert(NextRegistryIndex_ != std::numeric_limits<std::uint16_t>::max()
        && "ZoneRuntime::AllocateRegistryId: registry id index overflow");

    return RegistryId{ NextRegistryIndex_++, 1 };
}

void ZoneRuntime::InvalidateFrameScratch()
{
    std::fill(VisibleScratch.begin(), VisibleScratch.end(), nullptr);
    std::fill(PhysicsScratch.begin(), PhysicsScratch.end(), nullptr);
    std::fill(LogicScratch.begin(), LogicScratch.end(), nullptr);
    std::fill(AudioScratch.begin(), AudioScratch.end(), nullptr);

    VisibleScratch.clear();
    PhysicsScratch.clear();
    LogicScratch.clear();
    AudioScratch.clear();
}
