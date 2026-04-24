#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/World.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
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
    ComponentId Id     = InvalidComponentId;
    size_t      Size   = 0;
    size_t      Align  = 0;

    // Blob storage for the component value (heap-allocated for simplicity in spike).
    std::unique_ptr<uint8_t[]> Data;

    // OnAdd hook to call after the component is placed (nullptr if not applicable).
    std::function<void(void*, World&, EntityId)> OnAddHook;
    // OnRemove hook to call before the component is removed.
    std::function<void(const void*, World&, EntityId)> OnRemoveHook;

    ComponentPayload() = default;
    ComponentPayload(ComponentPayload&&) = default;
    ComponentPayload& operator=(ComponentPayload&&) = default;
};

struct Command
{
    CommandKind Kind;
    EntityId    Entity; // for Add/Remove/Destroy; invalid for Create
    ComponentPayload Payload; // populated for Add/Remove; empty for Destroy/Create

    // For CreateEntity: the initial set of component payloads.
    std::vector<ComponentPayload> InitialComponents;

    Command() = default;
    Command(Command&&) = default;
    Command& operator=(Command&&) = default;
};

// ─── CommandBuffer ───────────────────────────────────────────────────────────
//
// Records structural mutations during system execution.
// Flushed by the scheduler at phase boundaries.
//
// Flush semantics (P13 mitigation):
//   - All Add commands targeting the same (entity, source archetype) are grouped.
//   - This means "add C to N entities all in archetype {A,B}" resolves to
//     one bulk move per source archetype, not N individual entity moves.
//   - Record order is preserved within a group (hooks fire in record order).
//
// Lifecycle hook restrictions:
//   - Hooks must not call AddComponent, RemoveComponent, CreateEntity, or
//     DestroyEntity (directly or via a new CommandBuffer).
//   - Violation is caught by the QueryDepth guard in World during hook execution.

class CommandBuffer
{
public:
    explicit CommandBuffer(World& world) : W(&world) {}

    // Queue adding component T to entity.
    template <typename T>
    void AddComponent(EntityId entity, const T& value = T{})
    {
        Command cmd;
        cmd.Kind   = CommandKind::AddComponent;
        cmd.Entity = entity;

        cmd.Payload.Id    = InvalidComponentId; // resolved at flush
        cmd.Payload.Size  = sizeof(T);
        cmd.Payload.Align = alignof(T);

        if constexpr (sizeof(T) > 0)
        {
            cmd.Payload.Data = std::make_unique<uint8_t[]>(sizeof(T));
            std::memcpy(cmd.Payload.Data.get(), &value, sizeof(T));
        }

        // Capture type info for flush resolution.
        cmd.Payload.OnAddHook = [](void* ptr, World& w, EntityId e) {
            if constexpr (ComponentTraits<T>::HasOnAdd)
                ComponentTraits<T>::OnAdd(*static_cast<T*>(ptr), w, e);
        };

        // Tag used during flush to resolve ComponentId from type.
        TypeResolvers.push_back([this, idx = Commands.size()](World& w) {
            Commands[idx].Payload.Id = w.template GetComponentId<T>();
        });

        Commands.push_back(std::move(cmd));
    }

    template <typename T>
    void RemoveComponent(EntityId entity)
    {
        Command cmd;
        cmd.Kind   = CommandKind::RemoveComponent;
        cmd.Entity = entity;
        cmd.Payload.Id   = InvalidComponentId;
        cmd.Payload.Size = sizeof(T);

        cmd.Payload.OnRemoveHook = [](const void* ptr, World& w, EntityId e) {
            if constexpr (sizeof(T) > 0 && ComponentTraits<T>::HasOnRemove)
                ComponentTraits<T>::OnRemove(*static_cast<const T*>(ptr), w, e);
        };

        TypeResolvers.push_back([this, idx = Commands.size()](World& w) {
            Commands[idx].Payload.Id = w.template GetComponentId<T>();
        });

        Commands.push_back(std::move(cmd));
    }

    void DestroyEntity(EntityId entity)
    {
        Command cmd;
        cmd.Kind   = CommandKind::DestroyEntity;
        cmd.Entity = entity;
        Commands.push_back(std::move(cmd));
    }

    void CreateEntity() // minimal: no initial components in spike
    {
        Command cmd;
        cmd.Kind = CommandKind::CreateEntity;
        Commands.push_back(std::move(cmd));
    }

    // Flush all recorded commands to the world.
    // Must be called outside any active query.
    void Flush();

    bool IsEmpty() const { return Commands.empty(); }
    size_t Count() const  { return Commands.size(); }

    void Clear()
    {
        Commands.clear();
        TypeResolvers.clear();
    }

private:
    World* W;
    std::vector<Command> Commands;
    std::vector<std::function<void(World&)>> TypeResolvers;
};
