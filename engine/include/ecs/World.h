#pragma once

#include <ecs/Archetype.h>
#include <ecs/ArchetypeSignature.h>
#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/EntityRegistry.h>

#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Forward declarations — defined in their own headers.
class CommandBuffer;
template <typename... Accessors> class Query;

// ─── ComponentMeta ──────────────────────────────────────────────────────────

struct ComponentMeta
{
    ComponentId Id;
    size_t      Size;
    size_t      Alignment;
    bool        IsTag; // zero-size marker; no per-entity column
};

struct ComponentBatchItem
{
    EntityId    Entity;
    const void* Blob = nullptr;
};

// ─── World ──────────────────────────────────────────────────────────────────
//
// Owns: entity registry, archetype table, archetype graph, resources,
// and the query-scope guard.
// Single entry point for all ECS operations.

class World
{
    struct ScopedLifecycleHook
    {
        explicit ScopedLifecycleHook(World& world) : W(world) { ++W.LifecycleHookDepth; }
        ~ScopedLifecycleHook() { --W.LifecycleHookDepth; }

        World& W;
    };

public:
    // ── Registration ────────────────────────────────────────────────────────

    // Must be called before any entity is created (asserted in debug).
    // Calling after the first entity is created is UB in release.
    template <typename T>
    ComponentId RegisterComponent()
    {
        assert(!EntityCreated
               && "Component registration after entity creation is forbidden (v1).");

        const std::type_index ti(typeid(T));
        auto it = TypeToId.find(ti);
        if (it != TypeToId.end())
            return it->second;

        assert(NextComponentId < static_cast<ComponentId>(MaxComponents)
               && "Component budget (256) exceeded.");

        const ComponentId id = NextComponentId++;
        TypeToId[ti] = id;

        ComponentMeta meta{};
        meta.Id        = id;
        meta.Size      = std::is_empty_v<T> ? 0 : sizeof(T);
        meta.Alignment = std::is_empty_v<T> ? 1 : alignof(T);
        meta.IsTag     = std::is_empty_v<T>;
        ComponentMetas.push_back(meta);

        return id;
    }

    template <typename T>
    ComponentId GetComponentId() const
    {
        const std::type_index ti(typeid(T));
        auto it = TypeToId.find(ti);
        assert(it != TypeToId.end() && "Component type not registered");
        return it->second;
    }

    template <typename T>
    bool IsRegistered() const
    {
        return TypeToId.count(std::type_index(typeid(T))) > 0;
    }

    // ── Entity lifecycle ─────────────────────────────────────────────────────

    EntityId CreateEntity()
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "CreateEntity called while a query/lifecycle hook is active.");
        EntityCreated = true;
        EntityId id   = Entities.Create();
        Archetype* empty = GetOrCreateArchetype(ArchetypeSignature{});
        auto [ci, ri] = empty->AddRow(id.Index);
        Entities.SetLocation(id, EntityLocation{ empty->Id, ci, ri });
        return id;
    }

    // Create an entity whose initial archetype matches sig.
    // Component data must be written by the caller immediately after.
    EntityId CreateEntityWithSignature(const ArchetypeSignature& sig)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "CreateEntityWithSignature called while a query/lifecycle hook is active.");
        EntityCreated = true;
        EntityId id   = Entities.Create();
        Archetype* arch = GetOrCreateArchetype(sig);
        auto [ci, ri] = arch->AddRow(id.Index);
        Entities.SetLocation(id, EntityLocation{ arch->Id, ci, ri });
        return id;
    }

    void DestroyEntity(EntityId entity)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "DestroyEntity called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     arch = *ArchetypeList[loc.ArchetypeId];

        EntityIndex moved = arch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.Destroy(entity);
    }

    // ── Structural mutations ─────────────────────────────────────────────────

    template <typename T>
    void AddComponent(EntityId entity, const T& value = T{})
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "AddComponent called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));

        const ComponentId  id  = GetComponentId<T>();
        EntityLocation     loc = Entities.GetLocation(entity);
        Archetype&         src = *ArchetypeList[loc.ArchetypeId];

        assert(!src.Signature.test(id) && "Entity already has component T.");

        ArchetypeSignature newSig = src.Signature;
        newSig.set(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        if constexpr (!std::is_empty_v<T>)
            dst->WriteComponent(dci, dri, id, value);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });

        if constexpr (!std::is_empty_v<T> && ComponentTraits<T>::HasOnAdd)
        {
            const Chunk* ch  = dst->Chunks[dci].get();
            const uint32_t c = ch->FindColumn(id);
            T* ptr = reinterpret_cast<T*>(const_cast<uint8_t*>(ch->ColumnData(c))) + dri;
            ScopedLifecycleHook hookScope(*this);
            ComponentTraits<T>::OnAdd(*ptr, *this, entity);
        }
    }

    template <typename T>
    void RemoveComponent(EntityId entity)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "RemoveComponent called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));

        const ComponentId  id  = GetComponentId<T>();
        EntityLocation     loc = Entities.GetLocation(entity);
        Archetype&         src = *ArchetypeList[loc.ArchetypeId];

        assert(src.Signature.test(id) && "Entity does not have component T.");

        if constexpr (!std::is_empty_v<T> && ComponentTraits<T>::HasOnRemove)
        {
            const uint32_t c   = src.Chunks[loc.ChunkIndex]->FindColumn(id);
            const T*       ptr = reinterpret_cast<const T*>(
                src.Chunks[loc.ChunkIndex]->ColumnData(c)) + loc.RowIndex;
            ScopedLifecycleHook hookScope(*this);
            ComponentTraits<T>::OnRemove(*ptr, *this, entity);
        }

        ArchetypeSignature newSig = src.Signature;
        newSig.reset(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });
    }

    // ── Component access ─────────────────────────────────────────────────────

    template <typename T>
    T* TryGet(EntityId entity)
    {
        if (!Entities.IsAlive(entity)) return nullptr;
        const ComponentId  id  = GetComponentId<T>();
        const EntityLocation loc = Entities.GetLocation(entity);
        const Archetype&   arch = *ArchetypeList[loc.ArchetypeId];
        if (!arch.Signature.test(id)) return nullptr;
        const Chunk* chunk = arch.Chunks[loc.ChunkIndex].get();
        const uint32_t col = chunk->FindColumn(id);
        if (col == UINT32_MAX) return nullptr;
        return reinterpret_cast<T*>(
            const_cast<uint8_t*>(chunk->ColumnData(col))) + loc.RowIndex;
    }

    template <typename T>
    const T* TryGet(EntityId entity) const
    {
        return const_cast<World*>(this)->TryGet<T>(entity);
    }

    template <typename T>
    bool HasComponent(EntityId entity) const
    {
        if (!Entities.IsAlive(entity)) return false;
        const ComponentId id = GetComponentId<T>();
        return ArchetypeList[Entities.GetLocation(entity).ArchetypeId]->Signature.test(id);
    }

    // ── Archetype access (for Query internals) ───────────────────────────────

    std::vector<std::unique_ptr<Archetype>>&       GetArchetypes()       { return ArchetypeList; }
    const std::vector<std::unique_ptr<Archetype>>& GetArchetypes() const { return ArchetypeList; }

    // ── Query scope guard ────────────────────────────────────────────────────

    void PushQueryScope()         { ++QueryDepth; }
    void PopQueryScope()          { assert(QueryDepth > 0); --QueryDepth; }
    bool InQueryScope()     const { return QueryDepth > 0; }

    // ── Frame counter ────────────────────────────────────────────────────────

    uint32_t CurrentFrame() const { return FrameCounter; }
    void AdvanceFrame()           { ++FrameCounter; }

    // ── Resources ────────────────────────────────────────────────────────────

    template <typename T, typename... Args>
    T& AddResource(Args&&... args)
    {
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        T*   raw = ptr.get();
        Resources[std::type_index(typeid(T))] = {
            raw,
            [](void* p) { delete static_cast<T*>(p); }
        };
        ptr.release();
        return *raw;
    }

    template <typename T>
    T& GetResource()
    {
        auto it = Resources.find(std::type_index(typeid(T)));
        assert(it != Resources.end() && "Resource not registered");
        return *static_cast<T*>(it->second.first);
    }

    template <typename T>
    T* TryGetResource()
    {
        auto it = Resources.find(std::type_index(typeid(T)));
        return it != Resources.end() ? static_cast<T*>(it->second.first) : nullptr;
    }

    template <typename T>
    bool HasResource() const
    {
        return Resources.count(std::type_index(typeid(T))) > 0;
    }

    // ── Entity introspection ─────────────────────────────────────────────────

    bool   IsAlive(EntityId entity) const { return Entities.IsAlive(entity); }
    size_t EntityCount()            const { return Entities.Count(); }

    const ComponentMeta* GetMeta(ComponentId id) const
    {
        if (id >= ComponentMetas.size()) return nullptr;
        return &ComponentMetas[id];
    }

    // ── Type-erased mutation (used by CommandBuffer::Flush) ──────────────────
    //
    // These accept raw bytes and function pointers rather than templates so that
    // CommandBuffer can call them without knowing T at call time.
    // OnAddHook / OnRemoveHook may be null.

    void AddComponentRaw(
        EntityId entity,
        ComponentId id,
        const void* blob,
        size_t size,
        size_t /*align*/,
        void (*onAdd)(void*, World&, EntityId))
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(!src.Signature.test(id) && "Entity already has component.");

        ArchetypeSignature newSig = src.Signature;
        newSig.set(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        if (size > 0 && blob)
        {
            Chunk*         ch  = dst->Chunks[dci].get();
            const uint32_t col = ch->FindColumn(id);
            assert(col != UINT32_MAX);
            std::memcpy(ch->ColumnData(col) + dri * size, blob, size);
        }

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });

        if (onAdd && size > 0)
        {
            Chunk*         ch  = dst->Chunks[dci].get();
            const uint32_t col = ch->FindColumn(id);
            ScopedLifecycleHook hookScope(*this);
            onAdd(ch->ColumnData(col) + dri * size, *this, entity);
        }
    }

    void RemoveComponentRaw(
        EntityId entity,
        ComponentId id,
        void (*onRemove)(const void*, World&, EntityId))
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(src.Signature.test(id) && "Entity does not have component.");

        if (onRemove)
        {
            const Chunk*   ch  = src.Chunks[loc.ChunkIndex].get();
            const uint32_t col = ch->FindColumn(id);
            if (col != UINT32_MAX)
            {
                const void* ptr = ch->ColumnData(col) +
                                  loc.RowIndex * ch->Columns[col].Stride;
                ScopedLifecycleHook hookScope(*this);
                onRemove(ptr, *this, entity);
            }
        }

        ArchetypeSignature newSig = src.Signature;
        newSig.reset(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });
    }

    void AddComponentsRawBatch(
        ComponentId id,
        const ComponentBatchItem* items,
        size_t count,
        size_t size,
        size_t /*align*/)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");

        struct Move
        {
            EntityId       Entity;
            EntityLocation Source;
            EntityLocation Destination;
        };

        std::vector<Move> moves;
        moves.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const EntityId entity = items[i].Entity;
            if (!Entities.IsAlive(entity)) continue;

            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(!src.Signature.test(id) && "Entity already has component.");

            ArchetypeSignature newSig = src.Signature;
            newSig.set(id);
            Archetype* dst = GetOrCreateArchetype(newSig);

            auto [dci, dri] = dst->AddRow(entity.Index);
            dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

            if (size > 0 && items[i].Blob)
            {
                Chunk* ch = dst->Chunks[dci].get();
                const uint32_t col = ch->FindColumn(id);
                assert(col != UINT32_MAX);
                std::memcpy(ch->ColumnData(col) + dri * size, items[i].Blob, size);
            }

            moves.push_back(Move{
                entity,
                loc,
                EntityLocation{ dst->Id, dci, dri }
            });
        }

        RemoveSourceRowsInReverse(moves);

        for (const Move& move : moves)
            Entities.SetLocation(move.Entity, move.Destination);
    }

    void RemoveComponentsRawBatch(
        ComponentId id,
        const EntityId* entities,
        size_t count)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");

        struct Move
        {
            EntityId       Entity;
            EntityLocation Source;
            EntityLocation Destination;
        };

        std::vector<Move> moves;
        moves.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            const EntityId entity = entities[i];
            if (!Entities.IsAlive(entity)) continue;

            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(src.Signature.test(id) && "Entity does not have component.");

            ArchetypeSignature newSig = src.Signature;
            newSig.reset(id);
            Archetype* dst = GetOrCreateArchetype(newSig);

            auto [dci, dri] = dst->AddRow(entity.Index);
            dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

            moves.push_back(Move{
                entity,
                loc,
                EntityLocation{ dst->Id, dci, dri }
            });
        }

        RemoveSourceRowsInReverse(moves);

        for (const Move& move : moves)
            Entities.SetLocation(move.Entity, move.Destination);
    }

private:
    EntityRegistry                          Entities;
    std::vector<std::unique_ptr<Archetype>> ArchetypeList;

    struct SigHash
    {
        size_t operator()(const ArchetypeSignature& s) const noexcept
        {
            // FNV-1a over four 64-bit words extracted from the 256-bit bitset.
            // Avoids the to_string() heap allocation.
            // See docs/ecs/decisions.md D0.2 for benchmark impact.
            static constexpr size_t FNV_OFFSET = 14695981039346656037ULL;
            static constexpr size_t FNV_PRIME  = 1099511628211ULL;
            size_t h = FNV_OFFSET;
            for (int word = 0; word < 4; ++word)
            {
                const uint64_t w = (s >> (word * 64)).to_ulong();
                for (int b = 0; b < 8; ++b)
                {
                    h ^= static_cast<uint8_t>(w >> (b * 8));
                    h *= FNV_PRIME;
                }
            }
            return h;
        }
    };

    std::unordered_map<ArchetypeSignature, uint32_t, SigHash> SignatureToArchetype;

    std::vector<ComponentMeta>                         ComponentMetas;
    std::unordered_map<std::type_index, ComponentId>   TypeToId;
    ComponentId NextComponentId = 0;

    std::unordered_map<
        std::type_index,
        std::pair<void*, std::function<void(void*)>>> Resources;

    uint32_t QueryDepth   = 0;
    uint32_t LifecycleHookDepth = 0;
    uint32_t FrameCounter = 0;
    bool     EntityCreated = false;

    template <typename Move>
    void RemoveSourceRowsInReverse(const std::vector<Move>& moves)
    {
        for (size_t i = moves.size(); i > 0; --i)
        {
            const Move& move = moves[i - 1];
            EntityLocation current = Entities.GetLocation(move.Entity);
            Archetype& src = *ArchetypeList[current.ArchetypeId];
            EntityIndex moved = src.RemoveRow(current.ChunkIndex, current.RowIndex);
            if (moved != InvalidEntityIndex)
                Entities.SetLocationByIndex(moved, current);
        }
    }

    Archetype* GetOrCreateArchetype(const ArchetypeSignature& sig)
    {
        auto it = SignatureToArchetype.find(sig);
        if (it != SignatureToArchetype.end())
            return ArchetypeList[it->second].get();

        const uint32_t id = static_cast<uint32_t>(ArchetypeList.size());
        auto arch = std::make_unique<Archetype>();
        arch->Signature = sig;
        arch->Id        = id;

        std::vector<ComponentInfo> cols;
        for (const auto& meta : ComponentMetas)
            if (sig.test(meta.Id))
                cols.push_back(ComponentInfo{ meta.Id, meta.Size, meta.Alignment });

        arch->BuildLayout(cols);
        SignatureToArchetype[sig] = id;
        ArchetypeList.push_back(std::move(arch));
        return ArchetypeList.back().get();
    }
};
