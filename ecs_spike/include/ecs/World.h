#pragma once

#include <ecs/Archetype.h>
#include <ecs/ArchetypeSignature.h>
#include <ecs/ComponentId.h>
#include <ecs/ComponentTraits.h>
#include <ecs/EntityId.h>
#include <ecs/EntityRegistry.h>

#include <algorithm>
#include <any>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

// ─── forward declarations ────────────────────────────────────────────────────
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

// ─── World ──────────────────────────────────────────────────────────────────

// World: owns entity registry, archetype table, archetype graph,
// resources, and command-buffer pool.
// Single entry point for all ECS operations.
class World
{
public:
    // ── Registration ────────────────────────────────────────────────────────

    // Register a component type and assign it a ComponentId.
    // Must be called before any entity is created (enforced by assert).
    // Tag components (sizeof == 0) are detected via the IsTag specialization
    // path; pass an empty struct to get tag behaviour.
    template <typename T>
    ComponentId RegisterComponent()
    {
        assert(Entities.Count() == 0
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
        meta.Size      = sizeof(T);
        meta.Alignment = alignof(T);
        meta.IsTag     = (sizeof(T) == 0);
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
        EntityCreated = true;
        EntityId id = Entities.Create();
        // New entity has an empty signature; place it in the empty archetype.
        Archetype* empty = GetOrCreateArchetype(ArchetypeSignature{});
        auto [ci, ri] = empty->AddRow(id.Index);
        Entities.SetLocation(id, EntityLocation{ empty->Id, ci, ri });
        return id;
    }

    // Create entity and immediately add initial components.
    // Components are passed as (value, ...) pairs after registration.
    EntityId CreateEntity(const ArchetypeSignature& sig)
    {
        EntityCreated = true;
        EntityId id = Entities.Create();
        Archetype* arch = GetOrCreateArchetype(sig);
        auto [ci, ri] = arch->AddRow(id.Index);
        Entities.SetLocation(id, EntityLocation{ arch->Id, ci, ri });
        return id;
    }

    void DestroyEntity(EntityId entity)
    {
        assert(QueryDepth == 0
               && "DestroyEntity called while query is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc = Entities.GetLocation(entity);
        Archetype& arch = *ArchetypeList[loc.ArchetypeId];

        EntityIndex moved = arch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc); // row moved to same slot

        Entities.Destroy(entity);
    }

    // ── Structural mutations ─────────────────────────────────────────────────

    template <typename T>
    void AddComponent(EntityId entity, const T& value = T{})
    {
        assert(QueryDepth == 0
               && "AddComponent called while query is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));

        const ComponentId id = GetComponentId<T>();
        EntityLocation loc = Entities.GetLocation(entity);
        Archetype& srcArch = *ArchetypeList[loc.ArchetypeId];

        assert(!srcArch.Signature.test(id)
               && "Entity already has component T.");

        ArchetypeSignature newSig = srcArch.Signature;
        newSig.set(id);
        Archetype* dstArch = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dstArch->AddRow(entity.Index);

        // Copy shared components from old archetype.
        dstArch->CopySharedComponents(dci, dri, srcArch, loc.ChunkIndex, loc.RowIndex);

        // Write the new component.
        if constexpr (sizeof(T) > 0)
            dstArch->WriteComponent(dci, dri, id, value);

        // Remove from source archetype.
        EntityIndex moved = srcArch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        EntityLocation newLoc{ dstArch->Id, dci, dri };
        Entities.SetLocation(entity, newLoc);

        // Lifecycle hook.
        if constexpr (sizeof(T) > 0 && ComponentTraits<T>::HasOnAdd)
        {
            const Chunk* ch = dstArch->Chunks[dci].get();
            const uint32_t col = ch->FindColumn(id);
            T* ptr = reinterpret_cast<T*>(const_cast<uint8_t*>(ch->ColumnData(col))) + dri;
            ComponentTraits<T>::OnAdd(*ptr, *this, entity);
        }
    }

    template <typename T>
    void RemoveComponent(EntityId entity)
    {
        assert(QueryDepth == 0
               && "RemoveComponent called while query is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));

        const ComponentId id = GetComponentId<T>();
        EntityLocation loc = Entities.GetLocation(entity);
        Archetype& srcArch = *ArchetypeList[loc.ArchetypeId];

        assert(srcArch.Signature.test(id)
               && "Entity does not have component T.");

        // Lifecycle hook before removal.
        if constexpr (sizeof(T) > 0 && ComponentTraits<T>::HasOnRemove)
        {
            const uint32_t col = srcArch.Chunks[loc.ChunkIndex]->FindColumn(id);
            const T* ptr = reinterpret_cast<const T*>(
                srcArch.Chunks[loc.ChunkIndex]->ColumnData(col)) + loc.RowIndex;
            ComponentTraits<T>::OnRemove(*ptr, *this, entity);
        }

        ArchetypeSignature newSig = srcArch.Signature;
        newSig.reset(id);
        Archetype* dstArch = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dstArch->AddRow(entity.Index);
        dstArch->CopySharedComponents(dci, dri, srcArch, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = srcArch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dstArch->Id, dci, dri });
    }

    // ── Component access ─────────────────────────────────────────────────────

    template <typename T>
    T* TryGet(EntityId entity)
    {
        if (!Entities.IsAlive(entity)) return nullptr;
        const ComponentId id = GetComponentId<T>();
        const EntityLocation loc = Entities.GetLocation(entity);
        const Archetype& arch = *ArchetypeList[loc.ArchetypeId];
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

    std::vector<std::unique_ptr<Archetype>>& GetArchetypes() { return ArchetypeList; }
    const std::vector<std::unique_ptr<Archetype>>& GetArchetypes() const { return ArchetypeList; }

    // Query guard — prevents structural changes during iteration.
    void PushQueryScope()  { ++QueryDepth; }
    void PopQueryScope()   { assert(QueryDepth > 0); --QueryDepth; }
    bool InQueryScope() const { return QueryDepth > 0; }

    uint32_t CurrentFrame() const { return FrameCounter; }
    void AdvanceFrame() { ++FrameCounter; }

    // ── Resources ────────────────────────────────────────────────────────────

    template <typename T, typename... Args>
    T& AddResource(Args&&... args)
    {
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = ptr.get();
        // Erase to void* with type-safe deleter.
        using Deleter = std::function<void(void*)>;
        Deleter del = [](void* p) { delete static_cast<T*>(p); };
        ptr.release(); // ownership transferred to the erased holder below
        Resources[std::type_index(typeid(T))] = { raw, std::move(del) };
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

    // ── Entity introspection ─────────────────────────────────────────────────

    bool IsAlive(EntityId entity) const { return Entities.IsAlive(entity); }
    size_t EntityCount() const { return Entities.Count(); }

    const ComponentMeta* GetMeta(ComponentId id) const
    {
        if (id >= ComponentMetas.size()) return nullptr;
        return &ComponentMetas[id];
    }

    // ── Type-erased mutation (used by CommandBuffer::Flush) ──────────────────

    // Add a component by raw bytes. The blob must be sizeof(size) bytes aligned
    // to align. The OnAddHook is invoked after placement if non-null.
    void AddComponentRaw(
        EntityId entity,
        ComponentId id,
        const void* blob,
        size_t size,
        size_t align,
        const std::function<void(void*, World&, EntityId)>& onAdd)
    {
        assert(QueryDepth == 0 && "Structural change during active query.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc = Entities.GetLocation(entity);
        Archetype& srcArch = *ArchetypeList[loc.ArchetypeId];
        assert(!srcArch.Signature.test(id) && "Entity already has component.");

        ArchetypeSignature newSig = srcArch.Signature;
        newSig.set(id);
        Archetype* dstArch = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dstArch->AddRow(entity.Index);
        dstArch->CopySharedComponents(dci, dri, srcArch, loc.ChunkIndex, loc.RowIndex);

        // Write blob if non-tag.
        if (size > 0 && blob)
        {
            Chunk* dst = dstArch->Chunks[dci].get();
            const uint32_t col = dst->FindColumn(id);
            assert(col != UINT32_MAX);
            uint8_t* dst_ptr = dst->ColumnData(col) + dri * size;
            std::memcpy(dst_ptr, blob, size);
        }

        EntityIndex moved = srcArch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dstArch->Id, dci, dri });

        if (onAdd && size > 0)
        {
            Chunk* dst = dstArch->Chunks[dci].get();
            const uint32_t col = dst->FindColumn(id);
            void* ptr = dst->ColumnData(col) + dri * size;
            onAdd(ptr, *this, entity);
        }
    }

    // Remove a component by id. The OnRemoveHook is invoked before removal.
    void RemoveComponentRaw(
        EntityId entity,
        ComponentId id,
        const std::function<void(const void*, World&, EntityId)>& onRemove)
    {
        assert(QueryDepth == 0 && "Structural change during active query.");
        assert(Entities.IsAlive(entity));

        EntityLocation loc = Entities.GetLocation(entity);
        Archetype& srcArch = *ArchetypeList[loc.ArchetypeId];
        assert(srcArch.Signature.test(id) && "Entity does not have component.");

        if (onRemove)
        {
            const Chunk* chunk = srcArch.Chunks[loc.ChunkIndex].get();
            const uint32_t col = chunk->FindColumn(id);
            if (col != UINT32_MAX)
            {
                const void* ptr = chunk->ColumnData(col) + loc.RowIndex * chunk->Columns[col].Stride;
                onRemove(ptr, *this, entity);
            }
        }

        ArchetypeSignature newSig = srcArch.Signature;
        newSig.reset(id);
        Archetype* dstArch = GetOrCreateArchetype(newSig);

        auto [dci, dri] = dstArch->AddRow(entity.Index);
        dstArch->CopySharedComponents(dci, dri, srcArch, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = srcArch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dstArch->Id, dci, dri });
    }

private:
    EntityRegistry Entities;
    std::vector<std::unique_ptr<Archetype>> ArchetypeList;
    struct SigHash {
        size_t operator()(const ArchetypeSignature& s) const noexcept {
            // FNV-1a over the bitset's ulong words.
            // std::bitset doesn't expose raw words, but to_ulong() works for ≤64 bits.
            // For 256-bit signatures we hash 4 64-bit words extracted via shift.
            // This avoids the to_string() heap allocation.
            //
            // Approach: combine hash of (sig >> 0).to_ulong(), (sig >> 64).to_ulong(), etc.
            // Simpler for 256 bits: XOR four 64-bit FNV hashes.
            static constexpr size_t FNV_OFFSET = 14695981039346656037ULL;
            static constexpr size_t FNV_PRIME  = 1099511628211ULL;

            // We use a shift trick: copy into a uint64_t array via to_ulong on shifted copies.
            size_t h = FNV_OFFSET;
            for (int word = 0; word < 4; ++word)
            {
                // Extract 64-bit word 'word' from the bitset.
                // Shift right by word*64 and mask low 64 bits via to_ulong() on a copy.
                ArchetypeSignature shifted = s >> (word * 64);
                // to_ulong() truncates to the low 64 bits (which is what we want).
                const uint64_t w = (shifted >> 0).to_ulong(); // GCC: extracts low 64 bits
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

    std::vector<ComponentMeta> ComponentMetas;
    std::unordered_map<std::type_index, ComponentId> TypeToId;
    ComponentId NextComponentId = 0;

    // Resources: singleton objects keyed by type.
    // pair<raw ptr, deleter> — unique_ptr<void> requires a deleter lambda.
    std::unordered_map<std::type_index, std::pair<void*, std::function<void(void*)>>> Resources;

    uint32_t QueryDepth   = 0;
    uint32_t FrameCounter = 0;
    bool     EntityCreated = false;

    Archetype* GetOrCreateArchetype(const ArchetypeSignature& sig)
    {
        auto it = SignatureToArchetype.find(sig);
        if (it != SignatureToArchetype.end())
            return ArchetypeList[it->second].get();

        const uint32_t id = static_cast<uint32_t>(ArchetypeList.size());
        auto arch = std::make_unique<Archetype>();
        arch->Signature = sig;
        arch->Id        = id;

        // Build column layout from registered component metas.
        std::vector<ComponentInfo> cols;
        for (const auto& meta : ComponentMetas)
        {
            if (sig.test(meta.Id))
                cols.push_back(ComponentInfo{ meta.Id, meta.Size, meta.Alignment });
        }
        arch->BuildLayout(cols);

        SignatureToArchetype[sig] = id;
        ArchetypeList.push_back(std::move(arch));
        return ArchetypeList.back().get();
    }
};

