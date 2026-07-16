#pragma once

#include <ecs/Archetype.h>
#include <ecs/ArchetypeSignature.h>
#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <ecs/EntityRegistry.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Forward declarations — defined in their own headers.
class CommandBuffer;
class ResourceStore;
class EntityStore;
template <typename... Accessors> class Query;

// ─── ComponentMeta ──────────────────────────────────────────────────────────

struct ComponentMeta
{
    ComponentId      Id;
    ComponentTypeId  TypeId;    // stable, module-independent identity (replaces typeid key)
    std::string_view Name;      // the stable name behind TypeId, for diagnostics
    size_t           Size;
    size_t           Alignment;
    bool             IsTag;     // zero-size marker; no per-entity column
    void (*OnAdd)(void*, ResourceStore&, EntityId) = nullptr;
    void (*OnRemove)(const void*, ResourceStore&, EntityId) = nullptr;
};

struct ComponentBatchItem
{
    EntityId    Entity;
    const void* Blob = nullptr;
};

// ─── EntityStore ──────────────────────────────────────────────────────────────────
//
// Owns the entity registry, component metadata, archetype storage, change epochs,
// structural versioning, and query guards. Registry-scoped resources live outside it.
// Single entry point for all ECS operations.

class EntityStore
{
    struct ScopedLifecycleHook
    {
        explicit ScopedLifecycleHook(EntityStore& world) : W(world) { ++W.LifecycleHookDepth; }
        ~ScopedLifecycleHook() { --W.LifecycleHookDepth; }

        EntityStore& W;
    };

    struct ScopedQuery
    {
        explicit ScopedQuery(const EntityStore& world) : W(world) { W.PushQueryScope(); }
        ~ScopedQuery() { W.PopQueryScope(); }

        const EntityStore& W;
    };

    struct ScopedColumnWrite
    {
        ScopedColumnWrite(Chunk& chunk, uint32_t column, uint32_t frame)
            : Target(chunk)
            , Column(column)
            , Frame(frame)
        {
        }

        ~ScopedColumnWrite()
        {
            Target.BumpColumnVersion(Column, Frame);
        }

        Chunk& Target;
        uint32_t Column;
        uint32_t Frame;
    };

public:
    EntityStore() = default;
    explicit EntityStore(ResourceStore& lifecycleResources)
        : LifecycleResources(&lifecycleResources)
    {
    }

    ~EntityStore()
    {
        ClearEntities();
    }

    EntityStore(const EntityStore&) = delete;
    EntityStore& operator=(const EntityStore&) = delete;

    EntityStore(EntityStore&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

    EntityStore& operator=(EntityStore&& other) noexcept
    {
        if (this != &other)
        {
            ClearEntities();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    // ── Registration ────────────────────────────────────────────────────────

    // Must be called before any entity is created (asserted in debug).
    // Calling after the first entity is created is UB in release.
    template <typename T>
    ComponentId RegisterComponent()
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "Components must be trivially copyable: archetype chunks "
                      "relocate rows with memcpy.");
        assert(!EntityCreated
               && "Component registration after entity creation is forbidden (v1).");

        if constexpr (!std::is_empty_v<T>
                      && (ComponentHasOnAdd<T> || ComponentHasOnRemove<T>))
        {
            assert(LifecycleResources != nullptr
                   && "Lifecycle component registration requires a ResourceStore");
        }

        const ComponentTypeId key = ResolveComponentTypeId<T>();

        const size_t size  = std::is_empty_v<T> ? 0 : sizeof(T);
        const size_t align = std::is_empty_v<T> ? 1 : alignof(T);

        auto it = TypeToId.find(key);
        if (it != TypeToId.end())
        {
            // Same stable identity must mean the same storage contract — a
            // mismatch is two distinct types lying about a shared name, which
            // would silently corrupt archetype layout. Fail loudly (§3.3).
            const ComponentMeta& existing = ComponentMetas[it->second];
            assert(existing.Size == size && existing.Alignment == align
                   && existing.IsTag == std::is_empty_v<T>
                   && "ComponentTypeId collision: same stable name, different storage layout.");
            return it->second;
        }

        assert(NextComponentId < static_cast<ComponentId>(MaxComponents)
               && "Component budget (256) exceeded.");

        const ComponentId id = NextComponentId++;
        TypeToId[key] = id;

        ComponentMeta meta{};
        meta.Id        = id;
        meta.TypeId    = key;
        meta.Name      = ResolveComponentName<T>();
        meta.Size      = size;
        meta.Alignment = align;
        meta.IsTag     = std::is_empty_v<T>;
        if constexpr (!std::is_empty_v<T> && ComponentHasOnAdd<T>)
        {
            meta.OnAdd = [](void* ptr, ResourceStore& resources, EntityId entity) {
                ComponentTraits<T>::OnAdd(*static_cast<T*>(ptr), resources, entity);
            };
        }
        if constexpr (!std::is_empty_v<T> && ComponentHasOnRemove<T>)
        {
            meta.OnRemove = [](const void* ptr, ResourceStore& resources, EntityId entity) {
                ComponentTraits<T>::OnRemove(*static_cast<const T*>(ptr), resources, entity);
            };
        }
        ComponentMetas.push_back(meta);

        return id;
    }

    template <typename T>
    ComponentId GetComponentId() const
    {
        auto it = TypeToId.find(ResolveComponentTypeId<T>());
        assert(it != TypeToId.end() && "Component type not registered");
        return it->second;
    }

    template <typename T>
    bool IsRegistered() const
    {
        return TypeToId.count(ResolveComponentTypeId<T>()) > 0;
    }

    // Runtime, type-erased identity lookup — for code paths that learned a
    // component only via its stable ComponentTypeId (loaded modules, the editor's
    // registry-driven inspector) and cannot name T. Returns InvalidComponentId if
    // the type is not registered in this EntityStore.
    ComponentId GetComponentIdByType(ComponentTypeId type) const
    {
        auto it = TypeToId.find(type);
        return it == TypeToId.end() ? InvalidComponentId : it->second;
    }

    bool IsRegistered(ComponentTypeId type) const
    {
        return TypeToId.count(type) > 0;
    }

    // ── Entity lifecycle ─────────────────────────────────────────────────────

    EntityId CreateEntity()
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "CreateEntity called while a query/lifecycle hook is active.");
        EntityCreated = true;
        ++StructuralCounter;
        EntityId id   = EntityIds.Create();
        Archetype* empty = GetOrCreateArchetype(ArchetypeSignature{});
        auto [ci, ri] = empty->AddRow(id.Index);
        EntityIds.SetLocation(id, EntityLocation{ empty->Id, ci, ri });
        return id;
    }

    // Create an entity whose initial archetype matches sig.
    // Component data must be written by the caller immediately after.
    EntityId CreateEntityWithSignature(const ArchetypeSignature& sig)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "CreateEntityWithSignature called while a query/lifecycle hook is active.");
        EntityCreated = true;
        ++StructuralCounter;
        EntityId id   = EntityIds.Create();
        Archetype* arch = GetOrCreateArchetype(sig);
        auto [ci, ri] = arch->AddRow(id.Index);
        EntityIds.SetLocation(id, EntityLocation{ arch->Id, ci, ri });
        return id;
    }

    void DestroyEntity(EntityId entity)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "DestroyEntity called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(EntityIds.IsAlive(entity));
        ++StructuralCounter;

        EntityLocation loc  = EntityIds.GetLocation(entity);
        Archetype&     arch = *ArchetypeList[loc.ArchetypeId];
        InvokeRemoveHooks(entity, arch, loc);

        EntityIndex moved = arch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            EntityIds.SetLocationByIndex(moved, loc);

        EntityIds.Destroy(entity);
    }

    void ClearEntities()
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "ClearEntities called while a query/lifecycle hook is active.");
        const std::vector<EntityId> alive = EntityIds.GetAliveEntities();
        for (EntityId entity : alive)
            DestroyEntity(entity);
    }

    // ── Structural mutations ─────────────────────────────────────────────────

    template <typename T>
    void AddComponent(EntityId entity, const T& value = T{})
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "AddComponent called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(EntityIds.IsAlive(entity));
        ++StructuralCounter;

        const ComponentId  id  = GetComponentId<T>();
        EntityLocation     loc = EntityIds.GetLocation(entity);
        Archetype&         src = *ArchetypeList[loc.ArchetypeId];

        assert(!src.Signature.test(id) && "Entity already has component T.");

        ArchetypeSignature newSig = src.Signature;
        newSig.set(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        if constexpr (!std::is_empty_v<T>)
        {
            dst->WriteComponent(dci, dri, id, value);
            MarkComponentWritten(*dst, dci, id);
        }

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            EntityIds.SetLocationByIndex(moved, loc);

        EntityIds.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });

        if constexpr (!std::is_empty_v<T> && ComponentHasOnAdd<T>)
        {
            const Chunk* ch  = dst->Chunks[dci].get();
            const uint32_t c = ch->FindColumn(id);
            T* ptr = reinterpret_cast<T*>(const_cast<uint8_t*>(ch->ColumnData(c))) + dri;
            ScopedLifecycleHook hookScope(*this);
            ComponentTraits<T>::OnAdd(*ptr, LifecycleResourceStore(), entity);
        }
    }

    template <typename T>
    void RemoveComponent(EntityId entity)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "RemoveComponent called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(EntityIds.IsAlive(entity));
        ++StructuralCounter;

        const ComponentId  id  = GetComponentId<T>();
        EntityLocation     loc = EntityIds.GetLocation(entity);
        Archetype&         src = *ArchetypeList[loc.ArchetypeId];

        assert(src.Signature.test(id) && "Entity does not have component T.");

        if constexpr (!std::is_empty_v<T> && ComponentHasOnRemove<T>)
        {
            const uint32_t c   = src.Chunks[loc.ChunkIndex]->FindColumn(id);
            const T*       ptr = reinterpret_cast<const T*>(
                src.Chunks[loc.ChunkIndex]->ColumnData(c)) + loc.RowIndex;
            ScopedLifecycleHook hookScope(*this);
            ComponentTraits<T>::OnRemove(*ptr, LifecycleResourceStore(), entity);
        }

        ArchetypeSignature newSig = src.Signature;
        newSig.reset(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            EntityIds.SetLocationByIndex(moved, loc);

        EntityIds.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });
    }

    // ── Component access ─────────────────────────────────────────────────────

    // Non-const TryGet grants mutable access, so it conservatively bumps the
    // column version for T — the same semantics as Write<T> query access
    // (see docs/ecs/decisions.md D0.9 and D4.4). Use the const overload
    // (e.g. via std::as_const) for read-only access that must not register
    // as a change.
    template <typename T>
    T* TryGet(EntityId entity)
    {
        if (!EntityIds.IsAlive(entity)) return nullptr;
        const ComponentId  id  = GetComponentId<T>();
        const EntityLocation loc = EntityIds.GetLocation(entity);
        const Archetype&   arch = *ArchetypeList[loc.ArchetypeId];
        if (!arch.Signature.test(id)) return nullptr;
        Chunk* chunk = arch.Chunks[loc.ChunkIndex].get();
        const uint32_t col = chunk->FindColumn(id);
        if (col == UINT32_MAX) return nullptr;
        chunk->BumpColumnVersion(col, FrameCounter);
        return reinterpret_cast<T*>(chunk->ColumnData(col)) + loc.RowIndex;
    }

    template <typename T>
    const T* TryGet(EntityId entity) const
    {
        // Read-only access: no column-version bump (unlike the non-const overload).
        if (!EntityIds.IsAlive(entity)) return nullptr;
        const ComponentId  id  = GetComponentId<T>();
        const EntityLocation loc = EntityIds.GetLocation(entity);
        const Archetype&   arch = *ArchetypeList[loc.ArchetypeId];
        if (!arch.Signature.test(id)) return nullptr;
        const Chunk* chunk = arch.Chunks[loc.ChunkIndex].get();
        const uint32_t col = chunk->FindColumn(id);
        if (col == UINT32_MAX) return nullptr;
        return reinterpret_cast<const T*>(chunk->ColumnData(col)) + loc.RowIndex;
    }

    template <typename T>
    bool HasComponent(EntityId entity) const
    {
        if (!EntityIds.IsAlive(entity)) return false;
        if (!IsRegistered<T>()) return false;
        const ComponentId id = GetComponentId<T>();
        return ArchetypeList[EntityIds.GetLocation(entity).ArchetypeId]->Signature.test(id);
    }

    bool HasComponent(EntityId entity, ComponentId id) const
    {
        if (!EntityIds.IsAlive(entity)) return false;
        return ArchetypeList[EntityIds.GetLocation(entity).ArchetypeId]->Signature.test(id);
    }

    // ── Type-erased component access (by ComponentId) ─────────────────────────
    //
    // For code paths that know a component only by its runtime id — loaded game
    // modules, the editor's registry-driven inspector and RawComponentEditCommand
    // — and cannot name T. Returns a pointer to the component's raw bytes, or
    // nullptr if the entity is dead or lacks the component. The non-const overload
    // bumps the column version (mutable access), matching TryGet<T>.
    void* GetComponentRaw(EntityId entity, ComponentId id)
    {
        if (!EntityIds.IsAlive(entity)) return nullptr;
        const EntityLocation loc = EntityIds.GetLocation(entity);
        Archetype& arch = *ArchetypeList[loc.ArchetypeId];
        if (!arch.Signature.test(id)) return nullptr;
        Chunk* chunk = arch.Chunks[loc.ChunkIndex].get();
        const uint32_t col = chunk->FindColumn(id);
        if (col == UINT32_MAX) return nullptr;
        chunk->BumpColumnVersion(col, FrameCounter);
        return chunk->ColumnData(col) + loc.RowIndex * chunk->Columns[col].Stride;
    }

    const void* GetComponentRaw(EntityId entity, ComponentId id) const
    {
        if (!EntityIds.IsAlive(entity)) return nullptr;
        const EntityLocation loc = EntityIds.GetLocation(entity);
        const Archetype& arch = *ArchetypeList[loc.ArchetypeId];
        if (!arch.Signature.test(id)) return nullptr;
        const Chunk* chunk = arch.Chunks[loc.ChunkIndex].get();
        const uint32_t col = chunk->FindColumn(id);
        if (col == UINT32_MAX) return nullptr;
        return chunk->ColumnData(col) + loc.RowIndex * chunk->Columns[col].Stride;
    }

    // Non-const ForEachComponent hands out mutable references, so it bumps
    // each visited chunk's column version for T — same conservative semantics
    // as Write<T> and non-const TryGet (decisions.md D4.4). The const overload
    // iterates without bumping.
    template <typename T, typename F>
    void ForEachComponent(F&& fn)
    {
        ScopedQuery queryScope(*this);
        const ComponentId id = GetComponentId<T>();
        for (auto& archPtr : ArchetypeList)
        {
            Archetype& arch = *archPtr;
            if (!arch.Signature.test(id))
                continue;

            for (auto& chunkPtr : arch.Chunks)
            {
                Chunk& chunk = *chunkPtr;
                if (chunk.IsEmpty())
                    continue;

                const uint32_t col = chunk.FindColumn(id);
                if (col == UINT32_MAX)
                    continue;

                ScopedColumnWrite writeScope(chunk, col, FrameCounter);
                auto values = chunk.ColumnSpan<T>(col);
                const EntityIndex* entities = chunk.EntityIndices();
                for (uint32_t row = 0; row < chunk.RowCount; ++row)
                    fn(EntityId{ entities[row], GenerationForIndex(entities[row]) }, values[row]);
            }
        }
    }

    template <typename T, typename F>
    void ForEachComponent(F&& fn) const
    {
        ScopedQuery queryScope(*this);
        const ComponentId id = GetComponentId<T>();
        for (const auto& archPtr : ArchetypeList)
        {
            const Archetype& arch = *archPtr;
            if (!arch.Signature.test(id))
                continue;

            for (const auto& chunkPtr : arch.Chunks)
            {
                const Chunk& chunk = *chunkPtr;
                if (chunk.IsEmpty())
                    continue;

                const uint32_t col = chunk.FindColumn(id);
                if (col == UINT32_MAX)
                    continue;

                auto values = chunk.ColumnSpan<T>(col);
                const EntityIndex* entities = chunk.EntityIndices();
                for (uint32_t row = 0; row < chunk.RowCount; ++row)
                    fn(EntityId{ entities[row], GenerationForIndex(entities[row]) }, values[row]);
            }
        }
    }

    template <typename T>
    size_t CountComponents() const
    {
        if (!IsRegistered<T>())
            return 0;

        size_t count = 0;
        const ComponentId id = GetComponentId<T>();
        for (const auto& archPtr : ArchetypeList)
        {
            const Archetype& arch = *archPtr;
            if (!arch.Signature.test(id))
                continue;

            for (const auto& chunkPtr : arch.Chunks)
                count += chunkPtr->RowCount;
        }
        return count;
    }

    // ── Archetype access (for Query internals) ───────────────────────────────

    std::vector<std::unique_ptr<Archetype>>&       GetArchetypes()       { return ArchetypeList; }
    const std::vector<std::unique_ptr<Archetype>>& GetArchetypes() const { return ArchetypeList; }

    // ── Query scope guard ────────────────────────────────────────────────────

    void PushQueryScope()   const { ++QueryDepth; }
    void PopQueryScope()    const { assert(QueryDepth > 0); --QueryDepth; }
    bool InQueryScope()     const { return QueryDepth > 0; }

    // ── Frame counter ────────────────────────────────────────────────────────

    uint32_t CurrentFrame() const { return FrameCounter; }
    void AdvanceFrame()           { ++FrameCounter; }

    // ── Structural version ───────────────────────────────────────────────────
    //
    // Monotonic counter bumped by every operation that can move entity rows:
    // entity create/destroy and component add/remove (direct or via command
    // buffer flush). Systems that cache chunk pointers or row addresses across
    // frames must key their invalidation off this value — archetype count is
    // NOT sufficient (moves into existing archetypes and swap-removes change
    // row locations without changing the archetype count). See decisions.md D4.4.

    uint64_t StructuralVersion() const { return StructuralCounter; }

    // ── Entity chunk location ────────────────────────────────────────────────
    //
    // Resolves the chunk and row currently holding an entity, for systems that
    // cache row pointers across frames (e.g. transform propagation). Returns
    // {nullptr, 0} for dead entities. The result is invalidated by any
    // structural change; pair cached results with StructuralVersion().
    // Does NOT bump any column version — pure address resolution.

    struct EntityChunkLocation
    {
        Chunk*   ChunkPtr = nullptr;
        uint32_t Row      = 0;
    };

    EntityChunkLocation LocateEntity(EntityId entity)
    {
        if (!EntityIds.IsAlive(entity)) return {};
        const EntityLocation loc = EntityIds.GetLocation(entity);
        return { ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get(),
                 loc.RowIndex };
    }

    // ── Entity introspection ─────────────────────────────────────────────────

    bool   IsAlive(EntityId entity) const { return EntityIds.IsAlive(entity); }
    size_t EntityCount()            const { return EntityIds.Count(); }
    std::vector<EntityId> GetAliveEntities() const { return EntityIds.GetAliveEntities(); }

    const ComponentMeta* GetMeta(ComponentId id) const
    {
        if (id >= ComponentMetas.size()) return nullptr;
        return &ComponentMetas[id];
    }

    // ── Type-erased mutation ─────────────────────────────────────────────────
    //
    // Storage layout and lifecycle dispatch come from registered ComponentMeta.
    // Callers provide identity and bytes only; they cannot override or skip hooks.

    void AddComponentRaw(EntityId entity, ComponentId id, const void* blob)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(EntityIds.IsAlive(entity));
        assert(id < ComponentMetas.size() && "Raw add requires a registered component id.");
        ++StructuralCounter;

        const ComponentMeta& meta = ComponentMetas[id];
        EntityLocation loc  = EntityIds.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(!src.Signature.test(id) && "Entity already has component.");

        ArchetypeSignature newSig = src.Signature;
        newSig.set(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        if (meta.Size > 0)
        {
            assert(blob != nullptr && "Non-tag raw add requires component bytes.");
            Chunk*         ch  = dst->Chunks[dci].get();
            const uint32_t col = ch->FindColumn(id);
            assert(col != UINT32_MAX);
            uint8_t* destination = ch->ColumnData(col) + dri * meta.Size;
            if (blob != nullptr)
                std::memcpy(destination, blob, meta.Size);
            else
                std::memset(destination, 0, meta.Size);
            ch->BumpColumnVersion(col, FrameCounter);
        }

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            EntityIds.SetLocationByIndex(moved, loc);

        EntityIds.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });

        if (meta.OnAdd != nullptr)
        {
            Chunk*         ch  = dst->Chunks[dci].get();
            const uint32_t col = ch->FindColumn(id);
            ScopedLifecycleHook hookScope(*this);
            meta.OnAdd(ch->ColumnData(col) + dri * meta.Size, LifecycleResourceStore(), entity);
        }
    }

    void RemoveComponentRaw(EntityId entity, ComponentId id)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(EntityIds.IsAlive(entity));
        assert(id < ComponentMetas.size() && "Raw remove requires a registered component id.");
        ++StructuralCounter;

        const ComponentMeta& meta = ComponentMetas[id];
        EntityLocation loc  = EntityIds.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(src.Signature.test(id) && "Entity does not have component.");

        if (meta.OnRemove != nullptr)
        {
            const Chunk*   ch  = src.Chunks[loc.ChunkIndex].get();
            const uint32_t col = ch->FindColumn(id);
            assert(col != UINT32_MAX);
            const void* ptr = ch->ColumnData(col)
                + loc.RowIndex * ch->Columns[col].Stride;
            ScopedLifecycleHook hookScope(*this);
            meta.OnRemove(ptr, LifecycleResourceStore(), entity);
        }

        ArchetypeSignature newSig = src.Signature;
        newSig.reset(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            EntityIds.SetLocationByIndex(moved, loc);

        EntityIds.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });
    }

    void AddComponentsRawBatch(
        ComponentId id,
        const ComponentBatchItem* items,
        size_t count)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(id < ComponentMetas.size() && "Raw batch add requires a registered component id.");
        const ComponentMeta& meta = ComponentMetas[id];
        assert(meta.OnAdd == nullptr && "Hooked components cannot use raw batch add.");
        ++StructuralCounter;

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
            if (!EntityIds.IsAlive(entity)) continue;

            EntityLocation loc = EntityIds.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(!src.Signature.test(id) && "Entity already has component.");

            ArchetypeSignature newSig = src.Signature;
            newSig.set(id);
            Archetype* dst = GetOrCreateArchetype(newSig);

            auto [dci, dri] = dst->AddRow(entity.Index);
            dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

            if (meta.Size > 0)
            {
                assert(items[i].Blob != nullptr && "Non-tag raw batch add requires component bytes.");
                Chunk* ch = dst->Chunks[dci].get();
                const uint32_t col = ch->FindColumn(id);
                assert(col != UINT32_MAX);
                uint8_t* destination = ch->ColumnData(col) + dri * meta.Size;
                if (items[i].Blob != nullptr)
                    std::memcpy(destination, items[i].Blob, meta.Size);
                else
                    std::memset(destination, 0, meta.Size);
                ch->BumpColumnVersion(col, FrameCounter);
            }

            moves.push_back(Move{
                entity,
                loc,
                EntityLocation{ dst->Id, dci, dri }
            });
        }

        RemoveSourceRowsInReverse(moves);

        for (const Move& move : moves)
            EntityIds.SetLocation(move.Entity, move.Destination);
    }

    void RemoveComponentsRawBatch(
        ComponentId id,
        const EntityId* entities,
        size_t count)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(id < ComponentMetas.size() && "Raw batch remove requires a registered component id.");
        assert(ComponentMetas[id].OnRemove == nullptr
               && "Hooked components cannot use raw batch remove.");
        ++StructuralCounter;

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
            if (!EntityIds.IsAlive(entity)) continue;

            EntityLocation loc = EntityIds.GetLocation(entity);
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
            EntityIds.SetLocation(move.Entity, move.Destination);
    }

private:
    EntityRegistry                          EntityIds;
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
    std::unordered_map<ComponentTypeId, ComponentId>   TypeToId;
    ComponentId NextComponentId = 0;

    ResourceStore* LifecycleResources = nullptr;

    mutable uint32_t QueryDepth = 0;
    uint32_t LifecycleHookDepth = 0;
    uint32_t FrameCounter = 0;
    uint64_t StructuralCounter = 0;
    bool     EntityCreated = false;

    void MoveFrom(EntityStore&& other)
    {
        EntityIds = std::move(other.EntityIds);
        ArchetypeList = std::move(other.ArchetypeList);
        SignatureToArchetype = std::move(other.SignatureToArchetype);
        ComponentMetas = std::move(other.ComponentMetas);
        TypeToId = std::move(other.TypeToId);
        NextComponentId = other.NextComponentId;
        LifecycleResources = other.LifecycleResources;
        QueryDepth = other.QueryDepth;
        LifecycleHookDepth = other.LifecycleHookDepth;
        FrameCounter = other.FrameCounter;
        StructuralCounter = other.StructuralCounter;
        EntityCreated = other.EntityCreated;

        other.LifecycleResources = nullptr;
        other.QueryDepth = 0;
        other.LifecycleHookDepth = 0;
        other.EntityCreated = false;
    }

    ResourceStore& LifecycleResourceStore()
    {
        assert(LifecycleResources != nullptr
               && "Lifecycle dispatch requires a ResourceStore");
        return *LifecycleResources;
    }

    void InvokeRemoveHooks(EntityId entity,
                           const Archetype& archetype,
                           EntityLocation location)
    {
        const Chunk& chunk = *archetype.Chunks[location.ChunkIndex];
        for (const ComponentMeta& meta : ComponentMetas)
        {
            if (meta.OnRemove == nullptr || !archetype.Signature.test(meta.Id))
                continue;

            const uint32_t column = chunk.FindColumn(meta.Id);
            assert(column != UINT32_MAX && "Lifecycle component column missing");
            const void* component = chunk.ColumnData(column)
                + location.RowIndex * chunk.Columns[column].Stride;
            ScopedLifecycleHook hookScope(*this);
            meta.OnRemove(component, LifecycleResourceStore(), entity);
        }
    }

    void MarkComponentWritten(Archetype& archetype,
                              uint32_t chunkIndex,
                              ComponentId component)
    {
        Chunk& chunk = *archetype.Chunks[chunkIndex];
        const uint32_t column = chunk.FindColumn(component);
        assert(column != UINT32_MAX && "Written component column missing");
        chunk.BumpColumnVersion(column, FrameCounter);
    }

    uint32_t GenerationForIndex(EntityIndex index) const
    {
        return EntityIds.GenerationForIndex(index);
    }

    template <typename Move>
    void RemoveSourceRowsInReverse(const std::vector<Move>& moves)
    {
        for (size_t i = moves.size(); i > 0; --i)
        {
            const Move& move = moves[i - 1];
            EntityLocation current = EntityIds.GetLocation(move.Entity);
            Archetype& src = *ArchetypeList[current.ArchetypeId];
            EntityIndex moved = src.RemoveRow(current.ChunkIndex, current.RowIndex);
            if (moved != InvalidEntityIndex)
                EntityIds.SetLocationByIndex(moved, current);
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
