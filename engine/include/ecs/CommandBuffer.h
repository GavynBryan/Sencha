#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <ecs/World.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

// ─── Command kinds ───────────────────────────────────────────────────────────

enum class CommandKind : uint8_t
{
    AddComponent,
    RemoveComponent,
    DestroyEntity,
    CreateEntity,
};

struct ComponentPayload
{
    ComponentId Id         = InvalidComponentId;
    size_t      Size       = 0;
    size_t      Align      = 1;
    size_t      DataOffset = 0;
    bool        HasData    = false;

    void (*OnAddHook)(void*, World&, EntityId) = nullptr;
    void (*OnRemoveHook)(const void*, World&, EntityId) = nullptr;

    ComponentPayload() = default;
};

// An entity a CommandBuffer will create at Flush. It names the creation so later
// commands in the same recording can address it, and it is deliberately not an
// EntityId: no row exists yet, so there is nothing the World could be asked about.
// Flush substitutes the real id.
//
// Mirrors ZoneLocalEntityId, which solves the same problem for a zone package
// built off the owner thread.
struct PendingEntity
{
    static constexpr std::uint32_t InvalidOrdinal =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t Ordinal = InvalidOrdinal;

    // Which recording this handle belongs to. Flush and Clear end one, so a handle
    // held across either asserts rather than silently resolving to whatever entity
    // later takes its ordinal.
    std::uint32_t Epoch = 0;

    [[nodiscard]] bool IsValid() const { return Ordinal != InvalidOrdinal; }
    friend bool operator==(PendingEntity, PendingEntity) = default;
};

struct Command
{
    CommandKind      Kind;
    EntityId         Entity;
    ComponentPayload Payload;

    // CreateEntity: which storage partition the new row belongs to.
    StoragePartitionId Partition = StoragePartitionId::Default();

    // Set when this command addresses a creation recorded earlier in the same
    // buffer rather than an existing entity; Entity is then empty until Flush
    // resolves it. For a CreateEntity command it is that creation's own ordinal.
    std::uint32_t PendingOrdinal = PendingEntity::InvalidOrdinal;

    Command() = default;
    Command(Command&&) = default;
    Command& operator=(Command&&) = default;
};

// ─── CommandBuffer ───────────────────────────────────────────────────────────
//
// Records structural mutations during system execution.
// Flushed by the scheduler (or manually) at phase boundaries.
//
// Flush semantics:
//   Commands execute in record order. Grouping by source archetype for bulk
//   moves is deferred to a later phase; record-order execution is already
//   correct and produces one entity move per command.
//
// Lifecycle hook restrictions (enforced by World's lifecycle-hook guard):
//   Hooks must not call AddComponent, RemoveComponent, CreateEntity, or
//   DestroyEntity — doing so will assertion-fail in debug builds.

class CommandBuffer
{
public:
    explicit CommandBuffer(World& world) : W(&world) {}

    template <typename T>
    void AddComponent(EntityId entity, const T& value = T{})
    {
        Command cmd = RecordAddComponent<T>(value);
        cmd.Entity = entity;
        Commands.push_back(std::move(cmd));
    }

    // The same, addressed to an entity this buffer has not created yet.
    template <typename T>
    void AddComponent(PendingEntity entity, const T& value = T{})
    {
        Command cmd = RecordAddComponent<T>(value);
        cmd.PendingOrdinal = OrdinalOf(entity);
        Commands.push_back(std::move(cmd));
    }

    template <typename T>
    void RemoveComponent(EntityId entity)
    {
        Command cmd;
        cmd.Kind   = CommandKind::RemoveComponent;
        cmd.Entity = entity;
        cmd.Payload.Id   = W->template GetComponentId<T>();
        cmd.Payload.Size = std::is_empty_v<T> ? 0 : sizeof(T);

        if constexpr (!std::is_empty_v<T> && ComponentHasOnRemove<T>)
        {
            cmd.Payload.OnRemoveHook = [](const void* ptr, World& w, EntityId e) {
                ComponentTraits<T>::OnRemove(*static_cast<const T*>(ptr), w, e);
            };
        }

        Commands.push_back(std::move(cmd));
    }

    void DestroyEntity(EntityId entity)
    {
        Command cmd;
        cmd.Kind   = CommandKind::DestroyEntity;
        cmd.Entity = entity;
        Commands.push_back(std::move(cmd));
    }

    // Records a creation in the given storage partition, to be realized at Flush,
    // and returns a handle later commands in this recording can address — so a
    // system can spawn an entity and give it components without leaving query
    // scope.
    //
    // A system iterating a streamed zone passes the partition of the chunk it is
    // iterating (`ChunkView::Partition()`), so the spawn belongs to that zone and
    // is unloaded with it. There is deliberately no ambient "current partition":
    // the partition is data that can be passed.
    //
    // The handle is not an EntityId and cannot be given to the World; the row does
    // not exist until Flush. Discarding it is fine when nothing needs to address
    // the entity.
    PendingEntity CreateEntity(StoragePartitionId partition)
    {
        Command cmd;
        cmd.Kind = CommandKind::CreateEntity;
        cmd.Partition = partition;
        cmd.PendingOrdinal = PendingCount;
        Commands.push_back(std::move(cmd));
        return PendingEntity{ PendingCount++, Epoch };
    }

    // The persistent partition, matching World::CreateEntity().
    PendingEntity CreateEntity()
    {
        return CreateEntity(StoragePartitionId::Default());
    }

    // Flush all recorded commands to the world.
    // Must be called outside any active query.
    void Flush();

    bool   IsEmpty() const { return Commands.empty(); }
    size_t Count()   const { return Commands.size(); }

    void Clear()
    {
        Commands.clear();
        PayloadArena.clear();
        EndRecording();
    }

private:
    World*               W;
    std::vector<Command> Commands;
    std::vector<uint8_t> PayloadArena;

    // Creations recorded so far, and which recording they belong to. Epoch starts
    // at one so a default-constructed PendingEntity never matches a live one.
    std::uint32_t PendingCount = 0;
    std::uint32_t Epoch = 1;

    void EndRecording()
    {
        PendingCount = 0;
        ++Epoch;
    }

    std::uint32_t OrdinalOf(PendingEntity entity) const
    {
        assert(entity.IsValid()
               && "PendingEntity was never returned by CreateEntity.");
        assert(entity.Epoch == Epoch
               && "PendingEntity belongs to an earlier recording — Flush and Clear "
                  "end one, and its ordinal now names a different entity.");
        assert(entity.Ordinal < PendingCount);
        return entity.Ordinal;
    }

    template <typename T>
    Command RecordAddComponent(const T& value)
    {
        Command cmd;
        cmd.Kind = CommandKind::AddComponent;

        cmd.Payload.Id    = W->template GetComponentId<T>();
        cmd.Payload.Size  = std::is_empty_v<T> ? 0 : sizeof(T);
        cmd.Payload.Align = std::is_empty_v<T> ? 1 : alignof(T);

        if constexpr (!std::is_empty_v<T>)
        {
            cmd.Payload.HasData = true;
            cmd.Payload.DataOffset = StorePayload(&value, sizeof(T), alignof(T));
        }

        if constexpr (!std::is_empty_v<T> && ComponentHasOnAdd<T>)
        {
            cmd.Payload.OnAddHook = [](void* ptr, World& w, EntityId e) {
                ComponentTraits<T>::OnAdd(*static_cast<T*>(ptr), w, e);
            };
        }

        return cmd;
    }

    size_t StorePayload(const void* data, size_t size, size_t align)
    {
        const size_t mask = align - 1;
        const size_t offset = (PayloadArena.size() + mask) & ~mask;
        if (offset + size > PayloadArena.size())
            PayloadArena.resize(offset + size);
        std::memcpy(PayloadArena.data() + offset, data, size);
        return offset;
    }

    const void* PayloadData(const ComponentPayload& payload) const
    {
        return payload.HasData ? PayloadArena.data() + payload.DataOffset : nullptr;
    }
};
