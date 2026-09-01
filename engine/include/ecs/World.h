#pragma once

#include <ecs/Archetype.h>
#include <ecs/ArchetypeSignature.h>
#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <ecs/EntityRegistry.h>
#include <ecs/StoragePartitionId.h>
#include <ecs/StoragePartitionSet.h>

#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

// Forward declarations — defined in their own headers.
class CommandBuffer;
class World;
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

    // Type-erased lifecycle dispatch, captured when T is registered. Every
    // structural path goes through these, including the typed ones: whether a
    // component has hooks is a fact about the component, and a translation unit
    // that cannot see ComponentTraits<T> must not be able to answer it
    // differently. Null when T declares no such hook, or is a tag.
    void (*OnAddHook)(void* component, World& world, EntityId entity) = nullptr;
    void (*OnRemoveHook)(const void* component, World& world, EntityId entity) = nullptr;
};

struct ComponentBatchItem
{
    EntityId    Entity;
    const void* Blob = nullptr;
};

// Structural fact emitted when a live entity changes storage partitions.
// Backends consume this journal to update secondary zone indices without
// teaching the ECS about physics, audio, navigation, or rendering.
struct EntityPartitionMove
{
    EntityId Entity;
    StoragePartitionId Previous;
    StoragePartitionId Current;
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
    World() = default;

    // Teardown order contract: live components' OnRemove hooks fire first,
    // while resources are still reachable, then resources are destroyed.
    // Reversed, retain/release components could not reach their services and
    // would leak their external handles on unload.
    ~World()
    {
        DrainRemoveHooks();
        ClearOwnedTypeErased(Resources);
    }

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    World(World&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

    World& operator=(World&& other) noexcept
    {
        if (this != &other)
        {
            DrainRemoveHooks();
            ClearOwnedTypeErased(Resources);
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

        const ComponentTypeId key = ResolveComponentTypeId<T>();

        const size_t size  = std::is_empty_v<T> ? 0 : sizeof(T);
        const size_t align = std::is_empty_v<T> ? 1 : alignof(T);

        constexpr bool hasOnAdd    = !std::is_empty_v<T> && ComponentHasOnAdd<T>;
        constexpr bool hasOnRemove = !std::is_empty_v<T> && ComponentHasOnRemove<T>;

        // Registration is where a component's traits are read for the whole
        // World, so a unit that cannot see them would decide there are none and
        // say nothing. The component's own header states that its feature
        // defines them; this is where that promise is kept.
        static_assert(!ComponentDeclaresTraits<T> || hasOnAdd || hasOnRemove
                          || ComponentOwesComponents<T>,
                      "This component declares ComponentTraits, but none are "
                      "visible here: include the feature schema unit that "
                      "defines them. Registering without them leaves whatever "
                      "they retain or index unowned, and drops the columns the "
                      "component cannot work without.");

        auto it = TypeToId.find(key);
        if (it != TypeToId.end())
        {
            // Same stable identity must mean the same storage contract — a
            // mismatch is two distinct types lying about a shared name, which
            // would silently corrupt archetype layout. Fail loudly (§3.3).
            [[maybe_unused]] const ComponentMeta& existing = ComponentMetas[it->second];
            assert(existing.Size == size && existing.Alignment == align
                   && existing.IsTag == std::is_empty_v<T>
                   && "ComponentTypeId collision: same stable name, different storage layout.");
            // And the same declared obligations. A translation unit that
            // registers T without T's ComponentTraits in scope records an empty
            // set; whichever registration ran first would then decide, for this
            // whole World, whether a component arrives with what it needs.
            assert(Provisioning[it->second].DeclaredOwed == DeclaredOwedIds<T>()
                   && "Component registered twice with different DerivedComponents: "
                      "one of the translation units cannot see the declaration.");
            // And the same lifecycle obligations, for the same reason. Presence
            // rather than the pointers themselves: a game module instantiates
            // its own copy of the dispatch, so two registrations of one
            // component legitimately carry different addresses.
            assert((existing.OnAddHook != nullptr) == hasOnAdd
                   && (existing.OnRemoveHook != nullptr) == hasOnRemove
                   && "Component registered twice with different lifecycle hooks: "
                      "one of the translation units cannot see ComponentTraits.");
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
        if constexpr (hasOnAdd)
        {
            meta.OnAddHook = [](void* ptr, World& w, EntityId e) {
                ComponentTraits<T>::OnAdd(*static_cast<T*>(ptr), w, e);
            };
        }
        if constexpr (hasOnRemove)
        {
            meta.OnRemoveHook = [](const void* ptr, World& w, EntityId e) {
                ComponentTraits<T>::OnRemove(*static_cast<const T*>(ptr), w, e);
            };
        }
        ComponentMetas.push_back(meta);

        // What T owes, and how to give it. Captured here because this is the
        // only place the World still knows T by type; everything downstream --
        // a command buffer flushing a recorded add, the editor adding a
        // component it knows only by id -- addresses components as ids and
        // could not expand the tuple or default-construct the value.
        ComponentProvisioning provisioning;
        provisioning.DeclaredOwed = DeclaredOwedIds<T>();
        provisioning.AddDefault = [](World& world, EntityId entity) {
            world.AddComponent<T>(entity, T{});
        };
        Provisioning.push_back(std::move(provisioning));

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
    // the type is not registered in this World.
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
        return CreateEntity(StoragePartitionId::Default());
    }

    EntityId CreateEntity(StoragePartitionId partition)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "CreateEntity called while a query/lifecycle hook is active.");
        EntityCreated = true;
        BumpStructural(partition);
        EntityId id = Entities.Create();
        Archetype* empty = GetOrCreateArchetype(ArchetypeSignature{});
        auto [ci, ri] = empty->AddRow(id.Index, partition, FrameCounter);
        Entities.SetLocation(id, EntityLocation{ empty->Id, ci, ri, partition });
        return id;
    }

    // Create an entity whose initial archetype matches sig.
    // Component data must be written by the caller immediately after.
    EntityId CreateEntityWithSignature(const ArchetypeSignature& sig)
    {
        return CreateEntityWithSignature(StoragePartitionId::Default(), sig);
    }

    EntityId CreateEntityWithSignature(
        StoragePartitionId partition,
        const ArchetypeSignature& sig)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "CreateEntityWithSignature called while a query/lifecycle hook is active.");
        EntityCreated = true;
        BumpStructural(partition);
        EntityId id = Entities.Create();
        Archetype* arch = GetOrCreateArchetype(sig);
        auto [ci, ri] = arch->AddRow(id.Index, partition, FrameCounter);
        Entities.SetLocation(id, EntityLocation{ arch->Id, ci, ri, partition });
        return id;
    }

    // Writes a component into a row whose archetype already carries its column,
    // and fires OnAdd exactly as AddComponent would. The counterpart to
    // CreateEntityWithSignature: together they build an entity at its final
    // signature for the cost of one row, instead of one archetype transition per
    // component. Returns false when the row does not carry T's column, which is
    // the caller's cue that the ordinary AddComponent path is required.
    //
    // A row created by signature is uninitialized storage — every column placed
    // in the signature must be written before the entity is observed, or it
    // carries whatever the reused slab held.
    template <typename T>
    bool InitializeComponent(EntityId entity, const T& value = T{})
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "InitializeComponent called while a query/lifecycle hook is active.");
        assert(Entities.IsAlive(entity));

        if constexpr (std::is_empty_v<T>)
        {
            // Tag presence is the signature; there is no column to write and
            // AddComponent fires no hook for tags either.
            const EntityLocation loc = Entities.GetLocation(entity);
            if (!ArchetypeList[loc.ArchetypeId]->Signature.test(GetComponentId<T>()))
                return false;
        }
        else
        {
            // Non-const TryGet bumps the column version, so a freshly imported
            // entity reads as Changed<T> for this frame.
            T* slot = TryGet<T>(entity);
            if (slot == nullptr)
                return false;

            *slot = value;
            if (auto* onAdd = ComponentMetas[GetComponentId<T>()].OnAddHook)
            {
                ScopedLifecycleHook hookScope(*this);
                onAdd(slot, *this, entity);
            }
        }

        // The same obligation the ordinary add carries. A caller that built the
        // row at its final signature already put the owed columns in it, and
        // this costs one signature test each; a caller that did not gets them
        // the slow way rather than an entity missing them.
        ProvideDerivedComponents(entity, GetComponentId<T>());
        return true;
    }

    void DestroyEntity(EntityId entity)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "DestroyEntity called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     arch = *ArchetypeList[loc.ArchetypeId];
        assert(arch.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
        BumpStructural(loc.Partition);

        // Hooks fire before the swap-remove so they observe the destroyed
        // entity's own component values, not a moved neighbor's.
        FireRemoveHooks(entity, loc);

        EntityIndex moved = arch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.Destroy(entity);
    }

    StoragePartitionId GetEntityPartition(EntityId entity) const
    {
        const EntityLocation loc = Entities.GetLocation(entity);
        [[maybe_unused]] const Chunk* chunk =
            ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get();
        assert(chunk->Partition == loc.Partition);
        return loc.Partition;
    }

    // Reconstructs the live generational id for a raw index obtained from an
    // active chunk query. Query rows are alive by construction; callers outside
    // query storage should continue to hold EntityId directly.
    EntityId ResolveEntityIndex(EntityIndex index) const
    {
        const EntityId entity{ index, Entities.GenerationForIndex(index) };
        assert(Entities.IsAlive(entity));
        return entity;
    }

    bool MoveEntityToPartition(EntityId entity, StoragePartitionId destination)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "MoveEntityToPartition called while a query/lifecycle hook is active.");
        assert(Entities.IsAlive(entity));

        const EntityLocation source = Entities.GetLocation(entity);
        if (source.Partition == destination)
            return false;

        Archetype& archetype = *ArchetypeList[source.ArchetypeId];
        assert(archetype.Chunks[source.ChunkIndex]->Partition == source.Partition);

        auto [destinationChunk, destinationRow] =
            archetype.AddRow(entity.Index, destination, FrameCounter);
        MigrateRow(
            archetype,
            destinationChunk,
            destinationRow,
            archetype,
            source.ChunkIndex,
            source.RowIndex);

        EntityIndex moved = archetype.RemoveRow(source.ChunkIndex, source.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, source);

        Entities.SetLocation(entity, EntityLocation{
            archetype.Id,
            destinationChunk,
            destinationRow,
            destination,
        });

        ++StructuralCounter;
        BumpPartitionStructural(source.Partition);
        BumpPartitionStructural(destination);
        PartitionMoves.push_back(EntityPartitionMove{ entity, source.Partition, destination });
        return true;
    }

    size_t DestroyPartition(StoragePartitionId partition)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "DestroyPartition called while a query/lifecycle hook is active.");

        std::vector<EntityId> entities;
        for (const auto& archetype : ArchetypeList)
        {
            for (const auto& chunk : archetype->Chunks)
            {
                if (chunk->Partition != partition || chunk->IsEmpty())
                    continue;
                for (uint32_t row = 0; row < chunk->RowCount; ++row)
                {
                    const EntityIndex index = chunk->EntityIndices()[row];
                    entities.push_back(EntityId{ index, Entities.GenerationForIndex(index) });
                }
            }
        }

        for (const EntityId entity : entities)
            DestroyEntity(entity);
        return entities.size();
    }

    std::span<const EntityPartitionMove> PendingPartitionMoves() const
    {
        return { PartitionMoves.data(), PartitionMoves.size() };
    }

    std::vector<EntityPartitionMove> ConsumePartitionMoves()
    {
        std::vector<EntityPartitionMove> result;
        result.swap(PartitionMoves);
        return result;
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
        assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
        BumpStructural(loc.Partition);

        assert(!src.Signature.test(id) && "Entity already has component T.");

        ArchetypeSignature newSig = src.Signature;
        newSig.set(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition, FrameCounter);
        MigrateRow(*dst, dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        if constexpr (!std::is_empty_v<T>)
            dst->WriteComponent(dci, dri, id, value);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });

        FireAddHook(entity, id, *dst, dci, dri);

        // After T is whole, because an owed component's own hook may look at
        // the entity and T is part of what it would see.
        ProvideDerivedComponents(entity, id);
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
        assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
        BumpStructural(loc.Partition);

        assert(src.Signature.test(id) && "Entity does not have component T.");

        FireRemoveHook(entity, id, *src.Chunks[loc.ChunkIndex], loc.RowIndex);

        ArchetypeSignature newSig = src.Signature;
        newSig.reset(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition, FrameCounter);
        MigrateRow(*dst, dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });
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
        if (!Entities.IsAlive(entity)) return nullptr;
        const ComponentId  id  = GetComponentId<T>();
        const EntityLocation loc = Entities.GetLocation(entity);
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
        if (!Entities.IsAlive(entity)) return nullptr;
        const ComponentId  id  = GetComponentId<T>();
        const EntityLocation loc = Entities.GetLocation(entity);
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
        if (!Entities.IsAlive(entity)) return false;
        if (!IsRegistered<T>()) return false;
        const ComponentId id = GetComponentId<T>();
        return ArchetypeList[Entities.GetLocation(entity).ArchetypeId]->Signature.test(id);
    }

    bool HasComponent(EntityId entity, ComponentId id) const
    {
        if (!Entities.IsAlive(entity)) return false;
        return ArchetypeList[Entities.GetLocation(entity).ArchetypeId]->Signature.test(id);
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
        if (!Entities.IsAlive(entity)) return nullptr;
        const EntityLocation loc = Entities.GetLocation(entity);
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
        if (!Entities.IsAlive(entity)) return nullptr;
        const EntityLocation loc = Entities.GetLocation(entity);
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

                auto values = chunk.ColumnSpan<T>(col);
                const EntityIndex* entities = chunk.EntityIndices();
                for (uint32_t row = 0; row < chunk.RowCount; ++row)
                    fn(EntityId{ entities[row], GenerationForIndex(entities[row]) }, values[row]);

                chunk.BumpColumnVersion(col, FrameCounter);
            }
        }
    }

    template <typename T, typename F>
    void ForEachComponent(F&& fn) const
    {
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

    // Holds the scope across a callback that can throw. A depth left elevated
    // rejects every later structural mutation for the lifetime of the World,
    // so the release cannot sit on the normal return path alone.
    struct QueryScope
    {
        explicit QueryScope(const World& world) : W(world) { W.PushQueryScope(); }
        ~QueryScope() { W.PopQueryScope(); }

        QueryScope(const QueryScope&) = delete;
        QueryScope& operator=(const QueryScope&) = delete;

        const World& W;
    };

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

    uint64_t StructuralVersion(StoragePartitionId partition) const
    {
        return partition.Value < PartitionStructuralCounters.size()
            ? PartitionStructuralCounters[partition.Value]
            : 0;
    }

    // Digest of one partition set's structural versions, for a consumer whose
    // cached work covers exactly that set. Per-partition counters are monotonic,
    // so the sum strictly increases when any member changes structurally and is
    // unaffected by changes in partitions outside the set — which is the point:
    // a spawn in a dormant zone must not invalidate an active zone's cache.
    //
    // Membership is deliberately not folded in. A consumer whose set changed has
    // to reconcile the difference rather than merely notice it, so it compares
    // the set itself; hashing membership here would let that comparison be
    // skipped by accident.
    uint64_t StructuralVersion(const StoragePartitionSet& partitions) const
    {
        uint64_t digest = 0;
        for (const StoragePartitionId partition : partitions.Members())
            digest += StructuralVersion(partition);
        return digest;
    }

    // ── Storage diagnostics ──────────────────────────────────────────────────
    //
    // Counts the work storage actually performed, so tests can bound it without
    // asserting on wall-clock time. Building a row at its final signature costs
    // zero migrations; adding the same components one at a time costs one per
    // component, each copying every column added before it.

    uint64_t RowMigrationCount() const { return RowMigrationCounter; }

    // Chunk census, walked on demand — diagnostics and bench only, never per
    // frame. EmptyChunkCount is the reclamation signal: slabs retained past the
    // last row that needed them.
    size_t ChunkCount() const
    {
        size_t count = 0;
        for (const auto& archetype : ArchetypeList)
            count += archetype->Chunks.size();
        return count;
    }

    size_t EmptyChunkCount() const
    {
        size_t count = 0;
        for (const auto& archetype : ArchetypeList)
            for (const auto& chunk : archetype->Chunks)
                count += chunk->IsEmpty() ? 1 : 0;
        return count;
    }

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
        StoragePartitionId Partition = StoragePartitionId::Default();
    };

    EntityChunkLocation LocateEntity(EntityId entity)
    {
        if (!Entities.IsAlive(entity)) return {};
        const EntityLocation loc = Entities.GetLocation(entity);
        return { ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get(),
                 loc.RowIndex,
                 loc.Partition };
    }

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
    const T* TryGetResource() const
    {
        auto it = Resources.find(std::type_index(typeid(T)));
        return it != Resources.end() ? static_cast<const T*>(it->second.first) : nullptr;
    }

    template <typename T>
    bool HasResource() const
    {
        return Resources.count(std::type_index(typeid(T))) > 0;
    }

    // ── Entity introspection ─────────────────────────────────────────────────

    bool   IsAlive(EntityId entity) const { return Entities.IsAlive(entity); }
    size_t EntityCount()            const { return Entities.Count(); }
    std::vector<EntityId> GetAliveEntities() const { return Entities.GetAliveEntities(); }

    const ComponentMeta* GetMeta(ComponentId id) const
    {
        if (id >= ComponentMetas.size()) return nullptr;
        return &ComponentMetas[id];
    }

    // How many components this World knows, so a caller that walks them does
    // not have to probe GetMeta until it returns null. Ids are dense from zero.
    [[nodiscard]] std::size_t RegisteredComponentCount() const
    {
        return ComponentMetas.size();
    }

    // Every component `entity` carries, in ascending id order; empty for a dead
    // entity. Caller-owned storage so a caller comparing two snapshots is not
    // allocating twice per comparison.
    //
    // The entity's whole shape, which nothing else could ask for: the typed
    // accessors need a type and GetMeta needs an id you already have. An
    // authoring surface showing what an entity is, and an undo taking back
    // what an add brought, both start here.
    void ComponentIdsOn(EntityId entity, std::vector<ComponentId>& out) const
    {
        out.clear();
        if (!Entities.IsAlive(entity))
            return;

        const EntityLocation loc = Entities.GetLocation(entity);
        const ArchetypeSignature& signature = ArchetypeList[loc.ArchetypeId]->Signature;
        for (ComponentId id = 0; id < ComponentMetas.size(); ++id)
            if (signature.test(id))
                out.push_back(id);
    }

    // What `id` declares it cannot work without, as stated -- not the
    // transitive closure, and not filtered to what this World registered. For a
    // consumer that wants to say which component another one came from; the
    // provisioning below is what applies it.
    [[nodiscard]] std::span<const ComponentTypeId> DeclaredOwedComponents(ComponentId id) const
    {
        if (id >= Provisioning.size())
            return {};
        return Provisioning[id].DeclaredOwed;
    }

    // Adds everything `id` owes that this World knows and `entity` does not
    // already carry, default-constructed, through the ordinary typed add --
    // which applies each provisioned component's own owed set, so one call
    // settles the whole closure and a cycle terminates on what is already
    // there. A component the World never registered is skipped, so a fixture
    // with a partial vocabulary stays valid.
    //
    // A caller that has to undo its own add diffs the entity's components
    // across the call rather than being handed a list: provisioning recurses,
    // so what one call put on an entity is not only what this loop touched.
    void ProvideDerivedComponents(EntityId entity, ComponentId id)
    {
        if (id >= Provisioning.size())
            return;

        // By value: nothing here registers a component, but the typed add below
        // re-enters this World, and copying the small id list keeps the loop
        // independent of the table's identity.
        const std::vector<ComponentTypeId> owed = Provisioning[id].DeclaredOwed;
        for (const ComponentTypeId type : owed)
        {
            const ComponentId owedId = GetComponentIdByType(type);
            if (owedId == InvalidComponentId || HasComponent(entity, owedId))
                continue;
            if (Provisioning[owedId].AddDefault != nullptr)
                Provisioning[owedId].AddDefault(*this, entity);
        }
    }

    // ── Type-erased mutation (used by CommandBuffer::Flush) ──────────────────
    //
    // These accept raw bytes and an id rather than templates so that
    // CommandBuffer can call them without knowing T at call time. Lifecycle
    // hooks come from the registration, exactly as they do for the typed paths.

    void AddComponentRaw(
        EntityId entity,
        ComponentId id,
        const void* blob,
        size_t size,
        size_t /*align*/)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
        BumpStructural(loc.Partition);
        assert(!src.Signature.test(id) && "Entity already has component.");

        ArchetypeSignature newSig = src.Signature;
        newSig.set(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition, FrameCounter);
        MigrateRow(*dst, dci, dri, src, loc.ChunkIndex, loc.RowIndex);

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

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });

        FireAddHook(entity, id, *dst, dci, dri);

        // Outside the hook scope, not inside it: provisioning ends in the typed
        // add, which refuses to run while a lifecycle hook is on the stack. The
        // ordering is otherwise the one the typed add documents -- the component
        // is whole before anything it owes arrives to look at it.
        ProvideDerivedComponents(entity, id);
    }

    void RemoveComponentRaw(EntityId entity, ComponentId id)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "Structural change during active query/lifecycle hook.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
        BumpStructural(loc.Partition);
        assert(src.Signature.test(id) && "Entity does not have component.");

        FireRemoveHook(entity, id, *src.Chunks[loc.ChunkIndex], loc.RowIndex);

        ArchetypeSignature newSig = src.Signature;
        newSig.reset(id);
        Archetype* dst = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition, FrameCounter);
        MigrateRow(*dst, dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });
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
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
            assert(!src.Signature.test(id) && "Entity already has component.");

            ArchetypeSignature newSig = src.Signature;
            newSig.set(id);
            Archetype* dst = GetOrCreateArchetype(newSig);

            auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition, FrameCounter);
            MigrateRow(*dst, dci, dri, src, loc.ChunkIndex, loc.RowIndex);

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
                EntityLocation{ dst->Id, dci, dri, loc.Partition }
            });
        }

        RemoveSourceRowsInReverse(moves);

        for (const Move& move : moves)
            Entities.SetLocation(move.Entity, move.Destination);

        // Last, and by entity rather than by the locations recorded above: the
        // first provisioned add relocates a row and every Destination in this
        // list goes stale with it. One id for the whole run, so the check for
        // "owes nothing" is paid once rather than per entity.
        if (!DeclaredOwedComponents(id).empty())
            for (const Move& move : moves)
                ProvideDerivedComponents(move.Entity, id);
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
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
            assert(src.Signature.test(id) && "Entity does not have component.");

            ArchetypeSignature newSig = src.Signature;
            newSig.reset(id);
            Archetype* dst = GetOrCreateArchetype(newSig);

            auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition, FrameCounter);
            MigrateRow(*dst, dci, dri, src, loc.ChunkIndex, loc.RowIndex);

            moves.push_back(Move{
                entity,
                loc,
                EntityLocation{ dst->Id, dci, dri, loc.Partition }
            });
        }

        RemoveSourceRowsInReverse(moves);

        for (const Move& move : moves)
            Entities.SetLocation(move.Entity, move.Destination);
    }

private:
    // How a component is provisioned when something else owes it. Filled at
    // registration, index-aligned with ComponentMetas.
    struct ComponentProvisioning
    {
        std::vector<ComponentTypeId>  DeclaredOwed;
        void (*AddDefault)(World&, EntityId) = nullptr;
    };

    EntityRegistry                          Entities;
    std::vector<std::unique_ptr<Archetype>> ArchetypeList;

    // Index-aligned with ArchetypeList: the component ids in this archetype
    // that carry an OnRemove hook, in column order. Empty for the common
    // hook-free archetype, so destruction pays one lookup and one branch.
    std::vector<std::vector<ComponentId>>   HookedRemoveIdsByArchetype;

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
    // Beside ComponentMetas rather than inside it: ComponentMeta travels
    // through installed headers, and this is editor and command-buffer
    // machinery no module reads.
    std::vector<ComponentProvisioning>                 Provisioning;
    std::unordered_map<ComponentTypeId, ComponentId>   TypeToId;
    ComponentId NextComponentId = 0;

    std::unordered_map<
        std::type_index,
        std::pair<void*, std::function<void(void*)>>> Resources;

    mutable uint32_t QueryDepth = 0;
    uint32_t LifecycleHookDepth = 0;
    uint32_t FrameCounter = 0;
    uint64_t StructuralCounter = 0;
    uint64_t RowMigrationCounter = 0;
    // One-entry memo for GetOrCreateArchetype; see the comment there.
    ArchetypeSignature LastArchetypeSignature;
    Archetype* LastArchetype = nullptr;
    std::vector<uint64_t> PartitionStructuralCounters{ 0 };
    std::vector<EntityPartitionMove> PartitionMoves;
    bool     EntityCreated = false;

    static void ClearOwnedTypeErased(
        std::unordered_map<std::type_index, std::pair<void*, std::function<void(void*)>>>& map)
    {
        for (auto& [_, value] : map)
        {
            if (value.first != nullptr && value.second)
                value.second(value.first);
        }
        map.clear();
    }

    void MoveFrom(World&& other)
    {
        Entities = std::move(other.Entities);
        ArchetypeList = std::move(other.ArchetypeList);
        HookedRemoveIdsByArchetype = std::move(other.HookedRemoveIdsByArchetype);
        SignatureToArchetype = std::move(other.SignatureToArchetype);
        ComponentMetas = std::move(other.ComponentMetas);
        Provisioning = std::move(other.Provisioning);
        TypeToId = std::move(other.TypeToId);
        NextComponentId = other.NextComponentId;
        Resources = std::move(other.Resources);
        QueryDepth = other.QueryDepth;
        LifecycleHookDepth = other.LifecycleHookDepth;
        FrameCounter = other.FrameCounter;
        StructuralCounter = other.StructuralCounter;
        RowMigrationCounter = other.RowMigrationCounter;
        // Dropped rather than moved: the memo is pure acceleration, and a fresh
        // world must not answer from the moved-from world's last lookup.
        LastArchetypeSignature.reset();
        LastArchetype = nullptr;
        other.LastArchetypeSignature.reset();
        other.LastArchetype = nullptr;
        PartitionStructuralCounters = std::move(other.PartitionStructuralCounters);
        PartitionMoves = std::move(other.PartitionMoves);
        EntityCreated = other.EntityCreated;

        other.Resources.clear();
        other.QueryDepth = 0;
        other.LifecycleHookDepth = 0;
        other.EntityCreated = false;
    }

    uint32_t GenerationForIndex(EntityIndex index) const
    {
        return Entities.GenerationForIndex(index);
    }

    // Every row copy between archetype rows funnels through here so the
    // migration count cannot drift from the operations that actually pay for
    // it. The copy is O(shared columns); the count is one increment.
    void MigrateRow(
        Archetype& destination,
        uint32_t destinationChunk,
        uint32_t destinationRow,
        Archetype& source,
        uint32_t sourceChunk,
        uint32_t sourceRow)
    {
        destination.CopySharedComponents(
            destinationChunk,
            destinationRow,
            source,
            sourceChunk,
            sourceRow);
        ++RowMigrationCounter;
    }

    void BumpPartitionStructural(StoragePartitionId partition)
    {
        if (partition.Value >= PartitionStructuralCounters.size())
            PartitionStructuralCounters.resize(partition.Value + 1, 0);
        ++PartitionStructuralCounters[partition.Value];
    }

    void BumpStructural(StoragePartitionId partition)
    {
        ++StructuralCounter;
        BumpPartitionStructural(partition);
    }

    // Fire one component's OnAdd, on a row that already carries its column.
    // Shared by the typed add and the type-erased one so the two cannot drift:
    // the component decides whether a hook runs, never the caller.
    void FireAddHook(
        EntityId entity, ComponentId id, Archetype& arch, uint32_t chunkIndex, uint32_t rowIndex)
    {
        auto* onAdd = ComponentMetas[id].OnAddHook;
        if (onAdd == nullptr)
            return;

        Chunk*         ch  = arch.Chunks[chunkIndex].get();
        const uint32_t col = ch->FindColumn(id);
        assert(col != UINT32_MAX && "Hooked component missing its column");
        ScopedLifecycleHook hookScope(*this);
        onAdd(ch->ColumnData(col) + rowIndex * ch->Columns[col].Stride, *this, entity);
    }

    // The counterpart, fired before the row leaves the archetype so the hook
    // still reads the component's own bytes.
    void FireRemoveHook(EntityId entity, ComponentId id, Chunk& chunk, uint32_t rowIndex)
    {
        auto* onRemove = ComponentMetas[id].OnRemoveHook;
        if (onRemove == nullptr)
            return;

        const uint32_t col = chunk.FindColumn(id);
        assert(col != UINT32_MAX && "Hooked component missing its column");
        const void* ptr = chunk.ColumnData(col) + rowIndex * chunk.Columns[col].Stride;
        ScopedLifecycleHook hookScope(*this);
        onRemove(ptr, *this, entity);
    }

    // Fire OnRemove for every hooked component of one live row, in column
    // (registration) order — deterministic. Entity destruction cannot name
    // component types, so dispatch goes through the pointers captured at
    // registration. One vector index + empty check for hook-free archetypes.
    void FireRemoveHooks(EntityId entity, const EntityLocation& loc)
    {
        const auto& hooked = HookedRemoveIdsByArchetype[loc.ArchetypeId];
        if (hooked.empty())
            return;

        Chunk* ch = ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get();
        ScopedLifecycleHook hookScope(*this);
        for (const ComponentId id : hooked)
        {
            const uint32_t col = ch->FindColumn(id);
            assert(col != UINT32_MAX && "Hooked component missing its column");
            const void* ptr = ch->ColumnData(col) + loc.RowIndex * ch->Columns[col].Stride;
            ComponentMetas[id].OnRemoveHook(ptr, *this, entity);
        }
    }

    // Teardown pass: fire OnRemove for every live hooked component so the
    // hook contract holds on World destruction (see the destructor comment).
    // Row storage stays intact while hooks run. No-op on moved-from worlds.
    void DrainRemoveHooks()
    {
        for (const auto& archPtr : ArchetypeList)
        {
            const auto& hooked = HookedRemoveIdsByArchetype[archPtr->Id];
            if (hooked.empty())
                continue;

            for (const auto& chunkPtr : archPtr->Chunks)
            {
                Chunk* ch = chunkPtr.get();
                for (uint32_t row = 0; row < ch->RowCount; ++row)
                {
                    const EntityIndex index = ch->EntityIndices()[row];
                    const EntityId entity{ index, Entities.GenerationForIndex(index) };
                    ScopedLifecycleHook hookScope(*this);
                    for (const ComponentId id : hooked)
                    {
                        const uint32_t col = ch->FindColumn(id);
                        const void* ptr =
                            ch->ColumnData(col) + row * ch->Columns[col].Stride;
                        ComponentMetas[id].OnRemoveHook(ptr, *this, entity);
                    }
                }
            }
        }
    }

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
        // Hashing a 256-bit signature and probing costs more than comparing it
        // to the last one resolved, and callers arrive in runs of equal
        // signature: a zone import builds thousands of entities that share an
        // archetype, and an archetype graph walk revisits the same destination.
        // One slot, so an alternating pattern simply misses and falls through.
        if (LastArchetype != nullptr && LastArchetypeSignature == sig)
            return LastArchetype;

        auto it = SignatureToArchetype.find(sig);
        if (it != SignatureToArchetype.end())
        {
            Archetype* found = ArchetypeList[it->second].get();
            LastArchetypeSignature = sig;
            LastArchetype = found;
            return found;
        }

        const uint32_t id = static_cast<uint32_t>(ArchetypeList.size());
        auto arch = std::make_unique<Archetype>();
        arch->Signature = sig;
        arch->Id        = id;

        std::vector<ComponentInfo> cols;
        std::vector<ComponentId>   hooked;
        for (const auto& meta : ComponentMetas)
        {
            if (!sig.test(meta.Id))
                continue;
            cols.push_back(ComponentInfo{ meta.Id, meta.Size, meta.Alignment });
            if (meta.OnRemoveHook != nullptr)
                hooked.push_back(meta.Id);
        }

        arch->BuildLayout(cols);
        SignatureToArchetype[sig] = id;
        ArchetypeList.push_back(std::move(arch));
        HookedRemoveIdsByArchetype.push_back(std::move(hooked));
        // Archetypes are held by unique_ptr, so growing the list does not move
        // the object this remembers.
        LastArchetypeSignature = sig;
        LastArchetype = ArchetypeList.back().get();
        return LastArchetype;
    }
};
