#pragma once

#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/FieldDelta.h>
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
    SetField,
    DeltaField,
};

struct ComponentPayload
{
    ComponentId Id         = InvalidComponentId;
    size_t      Size       = 0;
    size_t      Align      = 1;
    size_t      DataOffset = 0;
    bool        HasData    = false;

    // True when this component has a runtime transition dispatch registered
    // (an observer). Stamped at record time because coalescing (which consults
    // OnAddHook) cannot see the runtime ComponentId->dispatch table; a stamped
    // command is never merged into a batch, so every observed transition is
    // applied singly and fires its dispatch.
    bool        HasDispatch = false;

    // For the SetField / DeltaField kinds: the leaf field's byte offset within
    // the component, and (delta only) its numeric kind. Size holds the field
    // size and the arena data holds the new value (set) or the addend (delta).
    std::uint16_t    FieldOffset = 0;
    NumericFieldKind NumericKind = NumericFieldKind::Int32;

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
        cmd.Payload.HasDispatch = W->HasTransitionDispatch(cmd.Payload.Id);

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
        cmd.Payload.HasDispatch = W->HasTransitionDispatch(cmd.Payload.Id);

        if constexpr (!std::is_empty_v<T> && ComponentHasOnRemove<T>)
        {
            cmd.Payload.OnRemoveHook = [](const void* ptr, World& w, EntityId e) {
                ComponentTraits<T>::OnRemove(*static_cast<const T*>(ptr), w, e);
            };
        }

        Commands.push_back(std::move(cmd));
    }

    // Type-erased add for callers that only have a ComponentId and raw bytes
    // (a script VM enqueuing a script-defined component). `blob` is copied
    // into the arena, so it need not outlive the call; pass size 0 / null
    // blob for a tag component. Hooks are optional; a script component's
    // lifecycle runs through its serializer, not ComponentTraits, so they are
    // typically null. Same deferred flush as the templated overload.
    void AddComponentRaw(EntityId entity, ComponentId id, const void* blob, size_t size,
                         size_t align,
                         void (*onAdd)(void*, World&, EntityId) = nullptr)
    {
        Command cmd;
        cmd.Kind        = CommandKind::AddComponent;
        cmd.Entity      = entity;
        cmd.Payload.Id  = id;
        cmd.Payload.Size = size;
        cmd.Payload.Align = align == 0 ? 1 : align;
        cmd.Payload.HasDispatch = W->HasTransitionDispatch(id);
        cmd.Payload.OnAddHook = onAdd;
        if (size > 0 && blob != nullptr)
        {
            cmd.Payload.HasData = true;
            cmd.Payload.DataOffset = StorePayload(blob, size, cmd.Payload.Align);
        }
        Commands.push_back(std::move(cmd));
    }

    void RemoveComponentRaw(EntityId entity, ComponentId id, size_t size,
                            void (*onRemove)(const void*, World&, EntityId) = nullptr)
    {
        Command cmd;
        cmd.Kind        = CommandKind::RemoveComponent;
        cmd.Entity      = entity;
        cmd.Payload.Id  = id;
        cmd.Payload.Size = size;
        cmd.Payload.HasDispatch = W->HasTransitionDispatch(id);
        cmd.Payload.OnRemoveHook = onRemove;
        Commands.push_back(std::move(cmd));
    }

    void DestroyEntity(EntityId entity)
    {
        Command cmd;
        cmd.Kind   = CommandKind::DestroyEntity;
        cmd.Entity = entity;
        Commands.push_back(std::move(cmd));
    }

    // Deferred write of a leaf field: overwrite `size` bytes at `offset` within
    // component `id` on `entity` with a copy of `bytes` (taken now, into the
    // arena). Applied at flush; fires OnSet if the field has an observer and the
    // bytes change. This is the deferred foreign/observable field set.
    void SetFieldRaw(EntityId entity, ComponentId id, std::uint16_t offset,
                     const void* bytes, size_t size)
    {
        Command cmd;
        cmd.Kind             = CommandKind::SetField;
        cmd.Entity           = entity;
        cmd.Payload.Id       = id;
        cmd.Payload.Size     = size;
        cmd.Payload.FieldOffset = offset;
        if (size > 0 && bytes != nullptr)
        {
            cmd.Payload.HasData    = true;
            cmd.Payload.DataOffset = StorePayload(bytes, size, size == 0 ? 1 : size);
        }
        Commands.push_back(std::move(cmd));
    }

    // Deferred `field += delta`: read-modify-write of the numeric field. The
    // addend `bytes` is copied now; the typed add (wrapping for integers) runs at
    // flush, then OnSet fires if the value changes.
    void DeltaFieldRaw(EntityId entity, ComponentId id, std::uint16_t offset,
                       NumericFieldKind kind, const void* bytes, size_t size)
    {
        Command cmd;
        cmd.Kind             = CommandKind::DeltaField;
        cmd.Entity           = entity;
        cmd.Payload.Id       = id;
        cmd.Payload.Size     = size;
        cmd.Payload.FieldOffset = offset;
        cmd.Payload.NumericKind = kind;
        if (size > 0 && bytes != nullptr)
        {
            cmd.Payload.HasData    = true;
            cmd.Payload.DataOffset = StorePayload(bytes, size, size == 0 ? 1 : size);
        }
        Commands.push_back(std::move(cmd));
    }

    // Creates an entity with no initial components (empty archetype) at flush.
    // This does not expose the created EntityId. For fully initialized spawns,
    // create directly outside query scope or use a higher-level spawn request
    // that is realized at a safe scheduler boundary.
    void CreateEntity()
    {
        Command cmd;
        cmd.Kind = CommandKind::CreateEntity;
        Commands.push_back(std::move(cmd));
    }

    // Flush all recorded commands to the world.
    // Must be called outside any active query.
    //
    // The recorded commands are wave 0. Each committed transition fires its
    // runtime dispatch synchronously; a synchronous observer's deferred effects
    // append to a fresh next-wave buffer (installed as the world's active
    // reactions). After wave 0 the flush drains the next wave, and so on to
    // quiescence, breadth-first. A hard cycle guard (kMaxReactionWaves) fails
    // closed rather than looping forever.
    void Flush();

    // Upper bound on reaction waves in one drain. A well-formed observer cascade
    // converges quickly; exceeding this is a defect (a non-converging cascade),
    // not a tuning knob.
    static constexpr uint32_t kMaxReactionWaves = 64;

    bool   IsEmpty() const { return Commands.empty(); }
    size_t Count()   const { return Commands.size(); }

    void Clear()
    {
        Commands.clear();
        PayloadArena.clear();
    }

private:
    // Applies this buffer's recorded commands in record order (coalescing runs of
    // unobserved, hook-free same-id adds/removes into batches). Runtime dispatch
    // fires from the World cores as each transition commits. Does not touch the
    // reaction scope; the caller (Flush) owns the wave loop.
    void ApplyCommands();

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
