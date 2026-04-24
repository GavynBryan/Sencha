#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

#include <cassert>
#include <cstdint>
#include <cstring>
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

struct Command
{
    CommandKind      Kind;
    EntityId         Entity;
    ComponentPayload Payload;

    std::vector<ComponentPayload> InitialComponents; // for CreateEntity

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
        Command cmd;
        cmd.Kind   = CommandKind::AddComponent;
        cmd.Entity = entity;

        cmd.Payload.Id    = W->template GetComponentId<T>();
        cmd.Payload.Size  = std::is_empty_v<T> ? 0 : sizeof(T);
        cmd.Payload.Align = std::is_empty_v<T> ? 1 : alignof(T);

        if constexpr (!std::is_empty_v<T>)
        {
            cmd.Payload.HasData = true;
            cmd.Payload.DataOffset = StorePayload(&value, sizeof(T), alignof(T));
        }

        if constexpr (!std::is_empty_v<T> && ComponentTraits<T>::HasOnAdd)
        {
            cmd.Payload.OnAddHook = [](void* ptr, World& w, EntityId e) {
                ComponentTraits<T>::OnAdd(*static_cast<T*>(ptr), w, e);
            };
        }

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

        if constexpr (!std::is_empty_v<T> && ComponentTraits<T>::HasOnRemove)
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

    // Creates an entity with no initial components (empty archetype).
    // To create with components, use CreateEntity() then queue AddComponent calls.
    void CreateEntity()
    {
        Command cmd;
        cmd.Kind = CommandKind::CreateEntity;
        Commands.push_back(std::move(cmd));
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
    }

private:
    World*               W;
    std::vector<Command> Commands;
    std::vector<uint8_t> PayloadArena;

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
