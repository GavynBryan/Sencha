#include <zone/ResolvedLinks.h>

#include <ecs/Query.h>
#include <ecs/World.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistableRef.h>
#include <world/transform/DerivedTransform.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace
{
    struct SideTrack
    {
        EntityId Aperture{};
        bool ZoneResident = false;
        // The frame the aperture had when its link first resolved; a later
        // frame that differs means the endpoint moved, which links do not support.
        std::optional<ApertureFrame> Pinned{};
    };

    struct LinkTrack
    {
        LinkId Id{};
        EntityId Link{};
        bool Spatial = false;
        SideTrack A{};
        SideTrack B{};
    };

    template <typename T>
    bool Before(const T& a, const T& b)
    {
        if (a.Id.Value != b.Id.Value)
            return a.Id.Value < b.Id.Value;
        return a.Link < b.Link;
    }

    template <typename T>
    const T* FindTracked(const std::vector<T>& sorted, LinkId id, EntityId link)
    {
        const T key{ .Id = id, .Link = link };
        const auto found = std::lower_bound(sorted.begin(), sorted.end(), key, Before<T>);
        return found != sorted.end() && found->Id == id && found->Link == link ? &*found : nullptr;
    }

    enum class SideOutcome : std::uint8_t
    {
        Resolved,
        Awaiting,
        Faulted,
    };

    struct SideResolution
    {
        SideOutcome Outcome = SideOutcome::Awaiting;
        LinkFault Fault = LinkFault::None;
        EntityId Aperture{};
        ApertureFrame Frame{};
        bool ZoneResident = false;
    };

    SideResolution ResolveSide(const RuntimeWorld& runtime, const PersistableRef& ref, ZoneId zone)
    {
        const World& world = runtime.Entities();
        SideResolution side;
        side.ZoneResident = runtime.IsZoneResident(zone);
        if (!side.ZoneResident)
            return side;
        side.Outcome = SideOutcome::Faulted;
        const EntityId entity = ResolvePersistableRef(world, ref);
        const LinkAperture* aperture = world.IsAlive(entity) ? world.TryGet<LinkAperture>(entity) : nullptr;
        if (aperture == nullptr)
        {
            side.Fault = LinkFault::NotAnAperture;
            return side;
        }
        side.Aperture = entity;
        if (runtime.ZoneOf(entity) != std::optional<ZoneId>(zone))
        {
            side.Fault = LinkFault::ZoneMismatch;
            return side;
        }
        const Transform3f pose = ComposeWorldTransform(world, entity);
        if (CheckApertureFrame(pose, aperture->HalfExtents) != ApertureFrameFault::None)
        {
            side.Fault = LinkFault::InvalidFrame;
            return side;
        }
        side.Frame = MakeApertureFrame(pose, aperture->HalfExtents);
        side.Outcome = SideOutcome::Resolved;
        return side;
    }

    // Keeps the first resolved frame of an aperture; true when this one differs.
    bool Moved(SideTrack& track, const SideTrack* previous, const SideResolution& side)
    {
        track.Aperture = side.Aperture;
        track.ZoneResident = side.ZoneResident;
        if (side.Outcome != SideOutcome::Resolved)
            return false;
        if (previous != nullptr && previous->Aperture == side.Aperture && previous->Pinned.has_value())
        {
            track.Pinned = previous->Pinned;
            return *previous->Pinned != side.Frame;
        }
        track.Pinned = side.Frame;
        return false;
    }
}

struct ResolvedLinks::Cache
{
    explicit Cache(const World& world)
        : Links(world)
        , ChangedLinks(world)
    {
        if (world.IsRegistered<LinkAperture>())
            ChangedApertures.emplace(world);
        if (world.IsRegistered<LinkAperture>() && world.IsRegistered<LocalTransform>())
            MovedApertures.emplace(world);
    }

    const World* Owner = nullptr;
    Query<Read<WorldLink>> Links;
    Query<Changed<WorldLink>> ChangedLinks;
    std::optional<Query<Changed<LinkAperture>>> ChangedApertures;
    std::optional<Query<Changed<LocalTransform>, With<LinkAperture>>> MovedApertures;
    std::uint32_t LastFrame = 0;
    bool Updated = false;
    std::vector<LinkTrack> Tracks;
};

ResolvedLinks::ResolvedLinks() = default;
ResolvedLinks::~ResolvedLinks() = default;

const char* LinkResolutionName(LinkResolution resolution)
{
    switch (resolution)
    {
    case LinkResolution::Absent: return "absent";
    case LinkResolution::DemandOnly: return "demand only";
    case LinkResolution::AwaitingA: return "awaiting A";
    case LinkResolution::AwaitingB: return "awaiting B";
    case LinkResolution::AwaitingBoth: return "awaiting both";
    case LinkResolution::Active: return "active";
    case LinkResolution::Invalid: return "invalid";
    }
    return "unknown";
}

const char* LinkFaultName(LinkFault fault)
{
    switch (fault)
    {
    case LinkFault::None: return "none";
    case LinkFault::OneEndpoint: return "names one aperture";
    case LinkFault::SameAperture: return "names one aperture at both ends";
    case LinkFault::ClaimedTwice: return "names an aperture another link names";
    case LinkFault::NotAnAperture: return "names no aperture in its resident zone";
    case LinkFault::ZoneMismatch: return "names an aperture outside the zone it says";
    case LinkFault::InvalidFrame: return "names an aperture with an invalid frame";
    case LinkFault::ExtentsDiffer: return "joins apertures of different sizes";
    case LinkFault::Moved: return "an aperture moved after it resolved";
    }
    return "unknown";
}

const ResolvedLink* ResolvedLinks::Find(LinkId id) const
{
    const auto found = std::lower_bound(Active_.begin(), Active_.end(), id,
                                        [](const ResolvedLink& link, LinkId key) { return link.Id.Value < key.Value; });
    return found != Active_.end() && found->Id == id ? &*found : nullptr;
}

void ResolvedLinks::Update(RuntimeWorld& runtime)
{
    const World& world = runtime.Entities();
    Transitions_.clear();
    Stats_.Rebuilds = 0;
    if (!world.IsRegistered<WorldLink>())
        return;
    if (Cache_ == nullptr || Cache_->Owner != &world)
    {
        Cache_ = std::make_unique<Cache>(world);
        Cache_->Owner = &world;
    }
    if (NeedsRebuild(runtime))
    {
        Rebuild(runtime);
        Stats_.Rebuilds = 1;
        ++Stats_.TotalRebuilds;
    }
    Cache_->LastFrame = world.CurrentFrame();
    Cache_->Updated = true;
}

bool ResolvedLinks::NeedsRebuild(const RuntimeWorld& runtime)
{
    Cache& cache = *Cache_;
    if (!cache.Updated)
        return true;
    // Writes made later in the frame of the last update carry that frame's
    // number, so the comparison starts one frame back.
    const std::uint32_t reference = cache.LastFrame > 0 ? cache.LastFrame - 1 : 0;
    bool changed = false;
    const auto any = [&changed](auto&) { changed = true; };
    cache.ChangedLinks.ForEachChunk(any, reference);
    if (!changed && cache.ChangedApertures)
        cache.ChangedApertures->ForEachChunk(any, reference);
    if (!changed && cache.MovedApertures)
        cache.MovedApertures->ForEachChunk(any, reference);
    if (changed)
        return true;

    const World& world = runtime.Entities();
    for (std::size_t index = 0; index < cache.Tracks.size(); ++index)
    {
        const LinkTrack& track = cache.Tracks[index];
        if (!world.IsAlive(track.Link))
            return true;
        if (!track.Spatial)
            continue;
        const LinkState& state = States_[index];
        for (const auto& [side, zone] : { std::pair{ &track.A, state.ZoneA }, std::pair{ &track.B, state.ZoneB } })
        {
            if (side->Aperture.IsValid() && !world.IsAlive(side->Aperture))
                return true;
            if (runtime.IsZoneResident(zone) != side->ZoneResident)
                return true;
        }
    }
    return false;
}

void ResolvedLinks::Rebuild(RuntimeWorld& runtime)
{
    Cache& cache = *Cache_;

    struct Authored
    {
        LinkId Id{};
        EntityId Link{};
        WorldLink Value{};
    };
    std::vector<Authored> authored;
    cache.Links.ForEachChunk([&](auto& view) {
        const std::span<const WorldLink> links = view.template Read<WorldLink>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            authored.push_back({ links[row].Id, view.Entity(row), links[row] });
    });
    std::sort(authored.begin(), authored.end(), Before<Authored>);

    std::unordered_map<std::uint64_t, std::uint32_t> claimants;
    for (const Authored& link : authored)
    {
        const PersistentEntityId a = link.Value.EndpointA.Target;
        const PersistentEntityId b = link.Value.EndpointB.Target;
        if (a.IsValid())
            ++claimants[a.Value];
        if (b.IsValid() && b != a)
            ++claimants[b.Value];
    }

    std::vector<LinkState> previousStates = std::move(States_);
    std::vector<LinkTrack> previousTracks = std::move(cache.Tracks);
    States_.clear();
    cache.Tracks.clear();
    Active_.clear();
    States_.reserve(authored.size());
    cache.Tracks.reserve(authored.size());

    for (const Authored& link : authored)
    {
        const WorldLink& value = link.Value;
        LinkState state{ .Id = link.Id, .Link = link.Link, .ZoneA = value.ZoneA, .ZoneB = value.ZoneB };
        LinkTrack track{ .Id = link.Id, .Link = link.Link };
        const LinkTrack* previous = FindTracked(previousTracks, link.Id, link.Link);
        const bool hasA = value.EndpointA.Target.IsValid();
        const bool hasB = value.EndpointB.Target.IsValid();
        track.Spatial = hasA && hasB;

        const auto fail = [&state](LinkFault fault) {
            state.Resolution = LinkResolution::Invalid;
            state.Fault = fault;
        };
        if (!hasA && !hasB)
            state.Resolution = LinkResolution::DemandOnly;
        else if (hasA != hasB)
            fail(LinkFault::OneEndpoint);
        else if (value.EndpointA.Target == value.EndpointB.Target)
            fail(LinkFault::SameAperture);
        else if (claimants[value.EndpointA.Target.Value] > 1 || claimants[value.EndpointB.Target.Value] > 1)
            fail(LinkFault::ClaimedTwice);
        else
        {
            const SideResolution a = ResolveSide(runtime, value.EndpointA, value.ZoneA);
            const SideResolution b = ResolveSide(runtime, value.EndpointB, value.ZoneB);
            const bool movedA = Moved(track.A, previous != nullptr ? &previous->A : nullptr, a);
            const bool movedB = Moved(track.B, previous != nullptr ? &previous->B : nullptr, b);
            if (a.Outcome == SideOutcome::Faulted)
                fail(a.Fault);
            else if (b.Outcome == SideOutcome::Faulted)
                fail(b.Fault);
            else if (a.Outcome == SideOutcome::Awaiting && b.Outcome == SideOutcome::Awaiting)
                state.Resolution = LinkResolution::AwaitingBoth;
            else if (a.Outcome == SideOutcome::Awaiting)
                state.Resolution = LinkResolution::AwaitingA;
            else if (b.Outcome == SideOutcome::Awaiting)
                state.Resolution = LinkResolution::AwaitingB;
            else if (std::abs(a.Frame.HalfExtents.X - b.Frame.HalfExtents.X) > ApertureTolerance::MatchingExtents
                     || std::abs(a.Frame.HalfExtents.Y - b.Frame.HalfExtents.Y) > ApertureTolerance::MatchingExtents)
                fail(LinkFault::ExtentsDiffer);
            else if (movedA || movedB)
                fail(LinkFault::Moved);
            else
            {
                state.Resolution = LinkResolution::Active;
                ResolvedLink& resolved = Active_.emplace_back();
                resolved.Id = link.Id;
                resolved.Link = link.Link;
                resolved.ApertureA = a.Aperture;
                resolved.ApertureB = b.Aperture;
                resolved.ZoneA = value.ZoneA;
                resolved.ZoneB = value.ZoneB;
                resolved.FrameA = a.Frame;
                resolved.FrameB = b.Frame;
                resolved.AToB = ApertureMapping(a.Frame, b.Frame);
                resolved.BToA = ApertureMapping(b.Frame, a.Frame);
                resolved.Directions = value.Directions;
                resolved.Upright = IsUprightMapping(resolved.AToB);
            }
        }
        States_.push_back(state);
        cache.Tracks.push_back(track);
    }

    for (const LinkState& state : States_)
    {
        const LinkState* before = FindTracked(previousStates, state.Id, state.Link);
        const LinkResolution from = before != nullptr ? before->Resolution : LinkResolution::Absent;
        const LinkFault fromFault = before != nullptr ? before->Fault : LinkFault::None;
        if (from != state.Resolution || fromFault != state.Fault)
            Transitions_.push_back({ .Id = state.Id, .From = from, .To = state.Resolution, .Fault = state.Fault });
    }
    for (const LinkState& before : previousStates)
        if (FindTracked(States_, before.Id, before.Link) == nullptr)
            Transitions_.push_back({ .Id = before.Id, .From = before.Resolution, .To = LinkResolution::Absent });
    std::stable_sort(Transitions_.begin(), Transitions_.end(),
                     [](const LinkTransition& a, const LinkTransition& b) { return a.Id.Value < b.Id.Value; });

    Stats_.Active = Stats_.Awaiting = Stats_.Invalid = Stats_.DemandOnly = 0;
    for (const LinkState& state : States_)
    {
        switch (state.Resolution)
        {
        case LinkResolution::Active: ++Stats_.Active; break;
        case LinkResolution::AwaitingA:
        case LinkResolution::AwaitingB:
        case LinkResolution::AwaitingBoth: ++Stats_.Awaiting; break;
        case LinkResolution::Invalid: ++Stats_.Invalid; break;
        case LinkResolution::DemandOnly: ++Stats_.DemandOnly; break;
        case LinkResolution::Absent: break;
        }
    }
}

std::optional<LinkContinuation> ContinueThroughLinks(std::span<const ResolvedLink> links, const Vec3d& from,
                                                     const Vec3d& to, float edgeTolerance)
{
    std::optional<LinkContinuation> first;
    for (const ResolvedLink& link : links)
    {
        for (const DockSide side : { DockSide::A, DockSide::B })
        {
            const std::uint32_t direction = side == DockSide::A ? DockDirectionAToB : DockDirectionBToA;
            if ((link.Directions & direction) == 0)
                continue;
            const ApertureFrame& entry = side == DockSide::A ? link.FrameA : link.FrameB;
            const std::optional<ApertureCrossing> crossing = CrossAperture(entry, from, to, edgeTolerance);
            if (!crossing || crossing->Direction != ApertureCrossingDirection::FrontToBack)
                continue;
            // Links arrive in id order and side A is tried first, so only a
            // strictly earlier crossing replaces the one held.
            if (first && crossing->Fraction >= first->Crossing.Fraction)
                continue;
            const Transform3f& mapping = side == DockSide::A ? link.AToB : link.BToA;
            first = LinkContinuation{
                .Link = &link,
                .Entered = side,
                .Crossing = *crossing,
                .Exit = mapping.TransformPoint(crossing->Point),
                .End = mapping.TransformPoint(to),
            };
        }
    }
    return first;
}
