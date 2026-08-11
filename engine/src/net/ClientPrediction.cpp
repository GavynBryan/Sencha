#include <net/ClientPrediction.h>

#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <net/ReplicationLayout.h>

#include <algorithm>
#include <cassert>
#include <utility>

void ClientPrediction::Bind(const ReplicationLayout& layout)
{
    assert(layout.IsSealed()
           && "Bind the predicted set from a sealed table: a component added "
              "afterwards would not have a slot to arrive in.");

    Slots.clear();
    std::uint32_t offset = 0;
    for (const ReplicatedComponent& component : layout.Components())
    {
        if (!component.Predicted)
            continue;

        ShadowSlot slot;
        slot.Type = component.Type;
        slot.Offset = offset;
        slot.Size = static_cast<std::uint32_t>(component.Size);
        Slots.push_back(slot);
        offset += slot.Size;
    }

    ShadowBytes.assign(offset, std::byte{});
    Reset();
}

const ClientPrediction::ShadowSlot* ClientPrediction::FindSlot(
    ComponentTypeId type) const
{
    for (const ShadowSlot& slot : Slots)
    {
        if (slot.Type == type)
            return &slot;
    }
    return nullptr;
}

ClientPrediction::ShadowSlot* ClientPrediction::FindSlot(ComponentTypeId type)
{
    return const_cast<ShadowSlot*>(std::as_const(*this).FindSlot(type));
}

bool ClientPrediction::Intercepts(EntityId entity, ComponentTypeId type) const
{
    if (!Enabled || !Predicts(entity))
        return false;
    return FindSlot(type) != nullptr;
}

std::span<std::byte> ClientPrediction::AuthoritativeBytes(ComponentTypeId type)
{
    const ShadowSlot* slot = FindSlot(type);
    if (slot == nullptr)
        return {};
    return std::span<std::byte>(ShadowBytes).subspan(slot->Offset, slot->Size);
}

bool ClientPrediction::HasAuthoritativeState(ComponentTypeId type) const
{
    const ShadowSlot* slot = FindSlot(type);
    return slot != nullptr && slot->Seen;
}

void ClientPrediction::MarkSeen(ComponentTypeId type)
{
    if (ShadowSlot* slot = FindSlot(type))
        slot->Seen = true;
}

bool ClientPrediction::RestoreTo(World& world, const WorldComponentSchema& schema,
                                 EntityId entity) const
{
    if (!entity.IsValid() || !world.IsAlive(entity))
        return false;

    bool restored = false;
    for (const ShadowSlot& slot : Slots)
    {
        if (!slot.Seen)
            continue;
        const std::span<const std::byte> bytes =
            std::span<const std::byte>(ShadowBytes).subspan(slot.Offset, slot.Size);
        if (schema.SetComponentBytes(world, entity, slot.Type, bytes))
            restored = true;
    }
    return restored;
}

void ClientPrediction::SetPredicted(EntityId entity)
{
    if (entity == Subject)
        return;
    // A new pawn means everything kept for the old one is about somebody else.
    Reset();
    Subject = entity;
}

void ClientPrediction::NoteReconcile(std::uint32_t ticksReplayed,
                                     float resetMeters, bool snapped)
{
    ++Reconciled;
    LastReplayed = ticksReplayed;
    LastReset = resetMeters;
    if (snapped)
        ++SnapCount;
}

void ClientPrediction::Reset()
{
    Ring.Clear();
    // The slots stay: which components this build predicts does not change
    // between sessions, and rebuilding them here would leave a client that has
    // just joined predicting nothing at all.
    for (ShadowSlot& slot : Slots)
        slot.Seen = false;
    std::fill(ShadowBytes.begin(), ShadowBytes.end(), std::byte{});
    Subject = EntityId{};
    LastReset = 0.0f;
    LastReplayed = 0;
    Reconciled = 0;
    SnapCount = 0;
}
