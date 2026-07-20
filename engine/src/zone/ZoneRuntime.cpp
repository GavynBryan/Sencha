#include <zone/ZoneRuntime.h>

#include <cassert>
#include <algorithm>
#include <limits>
#include <span>

ZoneRuntime::ZoneRuntime()
    : GlobalRegistry(std::make_unique<Registry>(MakeGlobalRegistry(RegistryId::Global())))
{
}

ZoneRuntime::~ZoneRuntime() = default;

Registry& ZoneRuntime::Global()
{
    return *GlobalRegistry;
}

const Registry& ZoneRuntime::Global() const
{
    return *GlobalRegistry;
}

Registry& ZoneRuntime::CreateZone(ZoneId zone)
{
    assert(zone.IsValid() && "ZoneRuntime::CreateZone: zone id must be valid");
    assert(!FindLoadedZone(zone) && "ZoneRuntime::CreateZone: duplicate zone id");

    assert(!FrameViewLive_
           && "CreateZone with a live frame view: zone lifecycle is drain-point-only");
    InvalidateFrameScratch();

    const RegistryId registryId = AllocateRegistryId();
    auto registry = std::make_unique<Registry>(MakeZoneRegistry(registryId, zone));

    auto loaded = std::make_unique<LoadedZone>();
    loaded->Zone = zone;
    loaded->ZoneRegistry = std::move(registry);
    loaded->Participation = {};

    assert(loaded->Zone == loaded->ZoneRegistry->Zone && "LoadedZone and Registry ZoneIds must match");

    Registry* registryPtr = loaded->ZoneRegistry.get();
    Zones.push_back(std::move(loaded));
    RecordAttached(registryPtr, ZoneParticipation{});
    return *registryPtr;
}

RegistryId ZoneRuntime::ReserveRegistryId()
{
    return AllocateRegistryId();
}

Registry& ZoneRuntime::AttachZone(std::unique_ptr<Registry> registry,
                                  ZoneParticipation participation)
{
    assert(!FrameViewLive_
           && "AttachZone with a live frame view: zone lifecycle is drain-point-only");
    assert(registry && "ZoneRuntime::AttachZone: registry must not be null");
    assert(registry->Kind == RegistryKind::Zone && "ZoneRuntime::AttachZone: registry kind must be Zone");
    assert(registry->Zone.IsValid() && "ZoneRuntime::AttachZone: registry must carry a valid ZoneId");
    assert(registry->Id.IsValid() && "ZoneRuntime::AttachZone: registry id must be reserved via ReserveRegistryId");
    assert(!FindLoadedZone(registry->Zone) && "ZoneRuntime::AttachZone: duplicate zone id");
    assert(!FindRegistry(registry->Id) && "ZoneRuntime::AttachZone: duplicate registry id");

    InvalidateFrameScratch();

    auto loaded = std::make_unique<LoadedZone>();
    loaded->Zone = registry->Zone;
    loaded->ZoneRegistry = std::move(registry);
    loaded->Participation = participation;

    Registry* registryPtr = loaded->ZoneRegistry.get();
    Zones.push_back(std::move(loaded));
    RecordAttached(registryPtr, participation);
    return *registryPtr;
}

bool ZoneRuntime::DestroyZone(ZoneId zone)
{
    auto it = std::find_if(Zones.begin(), Zones.end(),
        [zone](const std::unique_ptr<LoadedZone>& loaded) {
            return loaded->Zone == zone && !loaded->Detaching;
        });

    if (it == Zones.end())
        return false;

    assert(!FrameViewLive_
           && "DestroyZone with a live frame view: zone lifecycle is drain-point-only");
    InvalidateFrameScratch();

    // Two-step destruction: mark and record now. The registry is erased only
    // after its Detaching change reaches a residency phase, so every backend
    // owner gets a final visit while the registry and sibling resources live.
    LoadedZone& detaching = **it;
    detaching.Detaching = true;
    RecordDetaching(detaching.ZoneRegistry.get(), detaching.Participation);
    return true;
}

std::span<const RegistryResidencyChange> ZoneRuntime::ResidencyChanges() const
{
    return { PendingChanges_.data(), PendingChanges_.size() };
}

std::span<const RegistryResidencyChange> ZoneRuntime::BeginResidencyProcessing()
{
    assert(!FrameViewLive_
           && "BeginResidencyProcessing with a live frame view: residency is drain-point-only");
    assert(!ResidencyProcessing_
           && "BeginResidencyProcessing called before the previous batch was finalized");

    ProcessingChanges_.clear();
    ProcessingChanges_.swap(PendingChanges_);
    ResidencyProcessing_ = true;
    return { ProcessingChanges_.data(), ProcessingChanges_.size() };
}

void ZoneRuntime::FinalizeResidencyProcessing()
{
    assert(!FrameViewLive_
           && "FinalizeResidencyProcessing with a live frame view: residency is drain-point-only");
    assert(ResidencyProcessing_
           && "FinalizeResidencyProcessing called without BeginResidencyProcessing");

    // Only destroy registries whose Detaching transition was in the stable batch
    // just processed. A detach requested reentrantly during residency is queued
    // in PendingChanges_ and stays alive until next frame's final visit.
    std::erase_if(Zones, [this](const std::unique_ptr<LoadedZone>& loaded) {
        for (const RegistryResidencyChange& change : ProcessingChanges_)
        {
            if (change.Kind == RegistryResidencyChangeKind::Detaching
                && change.Id == loaded->ZoneRegistry->Id)
            {
                return true;
            }
        }
        return false;
    });

    ProcessingChanges_.clear();
    ResidencyProcessing_ = false;
}

RegistryResidencyChange* ZoneRuntime::FindPendingChange(RegistryId id)
{
    for (RegistryResidencyChange& change : PendingChanges_)
        if (change.Id == id)
            return &change;
    return nullptr;
}

void ZoneRuntime::RecordAttached(Registry* registry, ZoneParticipation participation)
{
    assert(FindPendingChange(registry->Id) == nullptr
           && "RecordAttached: registry ids are never reused within a window");
    PendingChanges_.push_back(RegistryResidencyChange{
        RegistryResidencyChangeKind::Attached,
        registry->Id,
        registry,
        ZoneParticipation{},
        participation,
    });
}

void ZoneRuntime::RecordParticipationChange(Registry* registry,
                                            ZoneParticipation previous,
                                            ZoneParticipation current)
{
    RegistryResidencyChange* existing = FindPendingChange(registry->Id);
    if (existing == nullptr)
    {
        PendingChanges_.push_back(RegistryResidencyChange{
            RegistryResidencyChangeKind::ParticipationChanged,
            registry->Id,
            registry,
            previous,
            current,
        });
        return;
    }

    // Coalesce to first-observed Previous and final Current: intermediate
    // states within one drain window were never observable to any frame.
    existing->Current = current;

    // A round trip (A -> B -> A) nets no change; drop the record so the
    // residency phase never processes a transition that did not happen.
    if (existing->Kind == RegistryResidencyChangeKind::ParticipationChanged
        && existing->Previous == existing->Current)
    {
        const RegistryId id = existing->Id;
        std::erase_if(PendingChanges_, [id](const RegistryResidencyChange& change) {
            return change.Id == id;
        });
    }
}

void ZoneRuntime::RecordDetaching(Registry* registry, ZoneParticipation previous)
{
    RegistryResidencyChange* existing = FindPendingChange(registry->Id);
    if (existing == nullptr)
    {
        PendingChanges_.push_back(RegistryResidencyChange{
            RegistryResidencyChangeKind::Detaching,
            registry->Id,
            registry,
            previous,
            ZoneParticipation{},
        });
        return;
    }

    if (existing->Kind == RegistryResidencyChangeKind::Attached)
    {
        // Finalizers run after AttachZone and before residency processing, so an
        // attach-then-destroy registry may already own entities and backend
        // resources. Preserve a Detaching visit rather than assuming there is
        // nothing to clean up.
        existing->Previous = existing->Current;
    }

    existing->Kind = RegistryResidencyChangeKind::Detaching;
    existing->Current = ZoneParticipation{};
}

bool ZoneRuntime::IsZoneLoaded(ZoneId zone) const
{
    return FindLoadedZone(zone) != nullptr;
}

Registry* ZoneRuntime::FindZone(ZoneId zone)
{
    LoadedZone* loaded = FindLoadedZone(zone);
    return loaded ? loaded->ZoneRegistry.get() : nullptr;
}

const Registry* ZoneRuntime::FindZone(ZoneId zone) const
{
    const LoadedZone* loaded = FindLoadedZone(zone);
    return loaded ? loaded->ZoneRegistry.get() : nullptr;
}

Registry* ZoneRuntime::FindRegistry(RegistryId id)
{
    if (!id.IsValid())
        return nullptr;

    if (GlobalRegistry && GlobalRegistry->Id == id)
        return GlobalRegistry.get();

    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->ZoneRegistry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (!loaded->Detaching && loaded->ZoneRegistry->Id == id)
            return loaded->ZoneRegistry.get();
    }

    return nullptr;
}

const Registry* ZoneRuntime::FindRegistry(RegistryId id) const
{
    if (!id.IsValid())
        return nullptr;

    if (GlobalRegistry && GlobalRegistry->Id == id)
        return GlobalRegistry.get();

    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->ZoneRegistry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (!loaded->Detaching && loaded->ZoneRegistry->Id == id)
            return loaded->ZoneRegistry.get();
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

    const ZoneParticipation previous = loaded->Participation;
    loaded->Participation = participation;
    if (!(previous == participation))
        RecordParticipationChange(loaded->ZoneRegistry.get(), previous, participation);
}

std::size_t ZoneRuntime::ZoneCount() const
{
    std::size_t count = 0;
    for (const auto& loaded : Zones)
        if (!loaded->Detaching)
            ++count;
    return count;
}

FrameRegistryView ZoneRuntime::BuildFrameView()
{
    FrameViewLive_ = true;
    InvalidateFrameScratch();

    // The global registry hosts world-lifetime gameplay state (the player pawn
    // and camera in a partitioned world) and is never dormant: it participates
    // in every span, ordered first so the frame's registry order is stable.
    VisibleScratch.push_back(GlobalRegistry.get());
    PhysicsScratch.push_back(GlobalRegistry.get());
    LogicScratch.push_back(GlobalRegistry.get());
    AudioScratch.push_back(GlobalRegistry.get());
    ParticipatingScratch.push_back(GlobalRegistry.get());

    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->ZoneRegistry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (loaded->Detaching)
            continue;

        Registry* registry = loaded->ZoneRegistry.get();
        const ZoneParticipation& participation = loaded->Participation;

        if (participation.Visible)
            VisibleScratch.push_back(registry);
        if (participation.Physics)
            PhysicsScratch.push_back(registry);
        if (participation.Logic)
            LogicScratch.push_back(registry);
        if (participation.Audio)
            AudioScratch.push_back(registry);
        if (participation.Any())
            ParticipatingScratch.push_back(registry);
    }

    return FrameRegistryView{
        .Global = GlobalRegistry.get(),
        .Visible = std::span<Registry*>{ VisibleScratch.data(), VisibleScratch.size() },
        .Physics = std::span<Registry*>{ PhysicsScratch.data(), PhysicsScratch.size() },
        .Logic = std::span<Registry*>{ LogicScratch.data(), LogicScratch.size() },
        .Audio = std::span<Registry*>{ AudioScratch.data(), AudioScratch.size() },
        .Participating = std::span<Registry* const>{ ParticipatingScratch.data(),
                                                     ParticipatingScratch.size() }
    };
}

ZoneRuntime::LoadedZone* ZoneRuntime::FindLoadedZone(ZoneId zone)
{
    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->ZoneRegistry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (!loaded->Detaching && loaded->Zone == zone)
            return loaded.get();
    }

    return nullptr;
}

const ZoneRuntime::LoadedZone* ZoneRuntime::FindLoadedZone(ZoneId zone) const
{
    for (const auto& loaded : Zones)
    {
        assert(loaded->Zone == loaded->ZoneRegistry->Zone && "LoadedZone and Registry ZoneIds must match");
        if (!loaded->Detaching && loaded->Zone == zone)
            return loaded.get();
    }

    return nullptr;
}

RegistryId ZoneRuntime::AllocateRegistryId()
{
    assert(NextRegistryIndex != std::numeric_limits<std::uint16_t>::max()
        && "ZoneRuntime::AllocateRegistryId: registry id index overflow");

    return RegistryId{ NextRegistryIndex++, 1 };
}

void ZoneRuntime::EndFrameView()
{
    FrameViewLive_ = false;
}

void ZoneRuntime::InvalidateFrameScratch()
{
    std::fill(VisibleScratch.begin(), VisibleScratch.end(), nullptr);
    std::fill(PhysicsScratch.begin(), PhysicsScratch.end(), nullptr);
    std::fill(LogicScratch.begin(), LogicScratch.end(), nullptr);
    std::fill(AudioScratch.begin(), AudioScratch.end(), nullptr);
    std::fill(ParticipatingScratch.begin(), ParticipatingScratch.end(), nullptr);

    VisibleScratch.clear();
    PhysicsScratch.clear();
    LogicScratch.clear();
    AudioScratch.clear();
    ParticipatingScratch.clear();
}
