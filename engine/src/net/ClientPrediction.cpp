#include <net/ClientPrediction.h>

#include <cstring>

bool ClientPrediction::Intercepts(EntityId entity, ComponentTypeId type) const
{
    return Enabled && Predicts(entity)
        && type == ResolveComponentTypeId<LocalTransform>();
}

std::span<std::byte> ClientPrediction::AuthoritativeBytes()
{
    return std::span<std::byte>(reinterpret_cast<std::byte*>(&Authoritative),
                                sizeof(Authoritative));
}

void ClientPrediction::Record(std::uint64_t authorityTick, const Vec3d& position)
{
    Sample& slot = Samples[authorityTick % kHistory];
    slot.Tick = authorityTick;
    slot.Position = position;
    slot.Filled = true;
}

std::optional<ClientPrediction::Correction> ClientPrediction::Commit(
    std::uint64_t authorityTick)
{
    SeenAuthority = true;

    const Sample& slot = Samples[authorityTick % kHistory];
    // A slot holds one tick at a time, so a tick older than the window has
    // already been overwritten by a newer one landing on the same index. The
    // stamp is what tells the two apart.
    if (!slot.Filled || slot.Tick != authorityTick)
        return std::nullopt;

    ++Observed;

    const Vec3d offset = Authoritative.Value.Position - slot.Position;
    const float distance = offset.Magnitude();
    LastError = distance;

    // Quantization and float noise, not divergence. Correcting it would jitter
    // a pawn that is already where it should be.
    if (distance < kIgnoreMeters)
        return std::nullopt;

    Correction correction;
    correction.Offset = offset;
    correction.Distance = distance;
    correction.Snap = distance >= SnapMeters;

    ++Corrected;
    if (correction.Snap)
        ++Snapped;
    return correction;
}

void ClientPrediction::Reset()
{
    Samples = {};
    Authoritative = LocalTransform{};
    SeenAuthority = false;
    Subject = EntityId{};
    LastError = 0.0f;
    Observed = 0;
    Corrected = 0;
    Snapped = 0;
}
