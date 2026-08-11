#include <net/ReplicationInterpolation.h>

#include <ecs/World.h>

#include <algorithm>

bool ReplicationInterpolation::Intercepts(ComponentTypeId type) const
{
    return Enabled && type == ResolveComponentTypeId<LocalTransform>();
}

void ReplicationInterpolation::Record(EntityId entity, std::uint64_t authorityTick,
                                      const LocalTransform& pose)
{
    if (!entity.IsValid())
        return;

    Track& track = Entities[entity];

    // The stream repeats itself: a tick already held has already been told, and
    // a second copy of it says nothing new.
    for (std::size_t index = 0; index < track.Count; ++index)
    {
        if (track.Samples[index].Tick == authorityTick)
            return;
    }

    // Older than everything held, with no room to keep it. Presentation has
    // already passed this tick, so it is history rather than a sample.
    if (track.Count == kSamples && authorityTick < track.Samples[0].Tick)
        return;

    if (track.Count == kSamples)
    {
        // Drop the oldest: the window moves forward, and a sample behind the
        // presented tick can no longer bracket it.
        std::move(track.Samples.begin() + 1, track.Samples.end(),
                  track.Samples.begin());
        --track.Count;
    }

    // Sorted insert. Datagrams arrive in order almost always, so this walks off
    // the end immediately; when one overtakes another it goes where it belongs
    // rather than making the window unsorted.
    std::size_t at = track.Count;
    while (at > 0 && track.Samples[at - 1].Tick > authorityTick)
    {
        track.Samples[at] = track.Samples[at - 1];
        --at;
    }

    track.Samples[at] = Sample{ authorityTick, pose, true };
    ++track.Count;
}

std::span<std::byte> ReplicationInterpolation::AuthoritativeBytes(EntityId entity)
{
    Track& track = Entities[entity];
    return std::span<std::byte>(reinterpret_cast<std::byte*>(&track.Authoritative),
                                sizeof(track.Authoritative));
}

bool ReplicationInterpolation::HasAuthoritativeState(EntityId entity) const
{
    const auto it = Entities.find(entity);
    return it != Entities.end() && it->second.SeenAuthority;
}

void ReplicationInterpolation::Commit(EntityId entity, std::uint64_t authorityTick)
{
    const auto it = Entities.find(entity);
    if (it == Entities.end())
        return;

    it->second.SeenAuthority = true;
    Record(entity, authorityTick, it->second.Authoritative);
}

std::optional<LocalTransform> ReplicationInterpolation::Resolve(
    EntityId entity, std::uint64_t presentTick) const
{
    const auto it = Entities.find(entity);
    if (it == Entities.end() || it->second.Count == 0)
        return std::nullopt;

    const Track& track = it->second;
    ++ResolvedTicks;

    // Before anything held. The entity has not been seen this early, so the
    // oldest pose is the whole truth about where it was.
    if (presentTick <= track.Samples[0].Tick)
    {
        ++HeldTicks;
        return track.Samples[0].Pose;
    }

    // Past everything held: the stream is further behind than the delay allows
    // for. Hold rather than guess -- an extrapolated pose has to be taken back,
    // and taking it back is a snap in the opposite direction.
    const Sample& newest = track.Samples[track.Count - 1];
    if (presentTick >= newest.Tick)
    {
        ++HeldTicks;
        return newest.Pose;
    }

    // Bracketed. The samples either side describe the path through whatever
    // ticks between them never arrived, which is what makes a lost snapshot
    // cost nothing visible.
    for (std::size_t index = track.Count - 1; index > 0; --index)
    {
        const Sample& before = track.Samples[index - 1];
        const Sample& after = track.Samples[index];
        if (presentTick < before.Tick || presentTick > after.Tick)
            continue;

        const auto span = static_cast<double>(after.Tick - before.Tick);
        const auto into = static_cast<double>(presentTick - before.Tick);
        const float alpha =
            span <= 0.0 ? 0.0f : static_cast<float>(std::clamp(into / span, 0.0, 1.0));

        ++InterpolatedTicks;
        LocalTransform blended;
        blended.Value =
            Transform3f::Interpolate(before.Pose.Value, after.Pose.Value, alpha);
        return blended;
    }

    ++HeldTicks;
    return newest.Pose;
}

void ReplicationInterpolation::Tracked(std::vector<EntityId>& out) const
{
    out.clear();
    out.reserve(Entities.size());
    for (const auto& [entity, track] : Entities)
        out.push_back(entity);
}

void ReplicationInterpolation::Forget(EntityId entity)
{
    Entities.erase(entity);
}

void ReplicationInterpolation::Reset()
{
    Entities.clear();
    ResolvedTicks = 0;
    HeldTicks = 0;
    InterpolatedTicks = 0;
}

void ReplicationInterpolationSystem::FixedLogic(FixedLogicContext& ctx)
{
    if (Interpolation == nullptr || Prediction == nullptr || Clock == nullptr)
        return;
    if (!Interpolation->IsEnabled() || Interpolation->TrackedCount() == 0)
        return;
    // Without a shared clock there is no name for the tick to present, and the
    // poses held are stamped in a numbering this machine cannot place.
    if (!Clock->HasEstimate())
        return;

    const std::uint64_t authorityNow = Clock->AuthorityTickAt(ctx.Time.TickIndex);
    // Flight is how long a datagram takes; the interval is how long the newest
    // sample had already been waiting before one was sent; the margin is what
    // is left over for jitter.
    const auto behind = static_cast<std::uint64_t>(Clock->FlightTicks())
                      + Interpolation->PresentationLagTicks();
    const std::uint64_t present = authorityNow > behind ? authorityNow - behind : 0;

    Interpolation->Tracked(Scratch);
    for (const EntityId entity : Scratch)
    {
        // The pawn this machine simulates for itself answers to the predictor,
        // which is arguing with the authority about this exact component. Two
        // writers would fight every tick. Dropped rather than skipped so the
        // poses do not sit there for the life of the session; if it ever stops
        // being predicted, the next snapshot starts the track again.
        if (Prediction->Predicts(entity))
        {
            Interpolation->Forget(entity);
            continue;
        }

        if (!ctx.Entities.IsAlive(entity))
        {
            Interpolation->Forget(entity);
            continue;
        }

        const std::optional<LocalTransform> pose =
            Interpolation->Resolve(entity, present);
        if (!pose.has_value())
            continue;

        if (LocalTransform* local = ctx.Entities.TryGet<LocalTransform>(entity))
            *local = *pose;
    }
}
