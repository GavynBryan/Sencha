from pathlib import Path

WORLD = Path("engine/include/ecs/World.h")
text = WORLD.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one match, found {count}: {old[:100]!r}")
    text = text.replace(old, new, 1)


replace_once(
    "#include <ecs/EntityRegistry.h>\n",
    "#include <ecs/EntityRegistry.h>\n#include <ecs/StoragePartitionId.h>\n",
)
replace_once(
    "#include <memory>\n#include <typeindex>\n",
    "#include <memory>\n#include <span>\n#include <typeindex>\n",
)
replace_once(
    "struct ComponentBatchItem\n{\n    EntityId    Entity;\n    const void* Blob = nullptr;\n};\n",
    "struct ComponentBatchItem\n{\n    EntityId    Entity;\n    const void* Blob = nullptr;\n};\n\n"
    "// Structural fact emitted when a live entity changes storage partitions.\n"
    "// Backends consume this journal to update secondary zone indices without\n"
    "// teaching the ECS about physics, audio, navigation, or rendering.\n"
    "struct EntityPartitionMove\n{\n"
    "    EntityId Entity;\n"
    "    StoragePartitionId Previous;\n"
    "    StoragePartitionId Current;\n"
    "};\n",
)

replace_once(
'''    EntityId CreateEntity()
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "CreateEntity called while a query/lifecycle hook is active.");
        EntityCreated = true;
        ++StructuralCounter;
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
        ++StructuralCounter;
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
        ++StructuralCounter;

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     arch = *ArchetypeList[loc.ArchetypeId];

        // Hooks fire before the swap-remove so they observe the destroyed
        // entity's own component values, not a moved neighbor's.
        FireRemoveHooks(entity, loc);

        EntityIndex moved = arch.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.Destroy(entity);
    }
''',
'''    EntityId CreateEntity()
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
        auto [ci, ri] = empty->AddRow(id.Index, partition);
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
        auto [ci, ri] = arch->AddRow(id.Index, partition);
        Entities.SetLocation(id, EntityLocation{ arch->Id, ci, ri, partition });
        return id;
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
        const Chunk* chunk = ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get();
        assert(chunk->Partition == loc.Partition);
        return loc.Partition;
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
            archetype.AddRow(entity.Index, destination);
        archetype.CopySharedComponents(
            destinationChunk,
            destinationRow,
            archetype,
            source.ChunkIndex,
            source.RowIndex);
        archetype.Chunks[destinationChunk]->BumpAllColumnVersions(FrameCounter);

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
''')

replace_once(
'''    template <typename T>
    void AddComponent(EntityId entity, const T& value = T{})
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "AddComponent called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));
        ++StructuralCounter;

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

        if constexpr (!std::is_empty_v<T> && ComponentHasOnAdd<T>)
        {
            const Chunk* ch  = dst->Chunks[dci].get();
            const uint32_t c = ch->FindColumn(id);
            T* ptr = reinterpret_cast<T*>(const_cast<uint8_t*>(ch->ColumnData(c))) + dri;
            ScopedLifecycleHook hookScope(*this);
            ComponentTraits<T>::OnAdd(*ptr, *this, entity);
        }
    }
''',
'''    template <typename T>
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

        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        if constexpr (!std::is_empty_v<T>)
            dst->WriteComponent(dci, dri, id, value);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });

        if constexpr (!std::is_empty_v<T> && ComponentHasOnAdd<T>)
        {
            const Chunk* ch  = dst->Chunks[dci].get();
            const uint32_t c = ch->FindColumn(id);
            T* ptr = reinterpret_cast<T*>(const_cast<uint8_t*>(ch->ColumnData(c))) + dri;
            ScopedLifecycleHook hookScope(*this);
            ComponentTraits<T>::OnAdd(*ptr, *this, entity);
        }
    }
''')

replace_once(
'''    template <typename T>
    void RemoveComponent(EntityId entity)
    {
        assert(QueryDepth == 0 && LifecycleHookDepth == 0
               && "RemoveComponent called while a query/lifecycle hook is active — use CommandBuffer.");
        assert(Entities.IsAlive(entity));
        ++StructuralCounter;

        const ComponentId  id  = GetComponentId<T>();
        EntityLocation     loc = Entities.GetLocation(entity);
        Archetype&         src = *ArchetypeList[loc.ArchetypeId];

        assert(src.Signature.test(id) && "Entity does not have component T.");

        if constexpr (!std::is_empty_v<T> && ComponentHasOnRemove<T>)
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
''',
'''    template <typename T>
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

        if constexpr (!std::is_empty_v<T> && ComponentHasOnRemove<T>)
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

        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition);
        dst->CopySharedComponents(dci, dri, src, loc.ChunkIndex, loc.RowIndex);

        EntityIndex moved = src.RemoveRow(loc.ChunkIndex, loc.RowIndex);
        if (moved != InvalidEntityIndex)
            Entities.SetLocationByIndex(moved, loc);

        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });
    }
''')

# Preserve partition on all type-erased structural paths. These replacements are
# deliberately local rather than regex-wide so an unexpected upstream change
# fails the one-shot patch instead of silently producing mixed-partition chunks.
replace_once(
'''        assert(Entities.IsAlive(entity));
        ++StructuralCounter;

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
''',
'''        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
        BumpStructural(loc.Partition);
''')
replace_once(
    "        auto [dci, dri] = dst->AddRow(entity.Index);\n",
    "        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition);\n",
)
replace_once(
    "        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });\n",
    "        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });\n",
)

replace_once(
'''        assert(Entities.IsAlive(entity));
        ++StructuralCounter;

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
''',
'''        assert(Entities.IsAlive(entity));

        EntityLocation loc  = Entities.GetLocation(entity);
        Archetype&     src  = *ArchetypeList[loc.ArchetypeId];
        assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
        BumpStructural(loc.Partition);
''')
replace_once(
    "        auto [dci, dri] = dst->AddRow(entity.Index);\n",
    "        auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition);\n",
)
replace_once(
    "        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri });\n",
    "        Entities.SetLocation(entity, EntityLocation{ dst->Id, dci, dri, loc.Partition });\n",
)

replace_once(
'''        ++StructuralCounter;

        struct Move
''',
'''        struct Move
''')
replace_once(
'''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
''',
'''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
''')
replace_once(
    "            auto [dci, dri] = dst->AddRow(entity.Index);\n",
    "            auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition);\n",
)
replace_once(
    "                EntityLocation{ dst->Id, dci, dri }\n",
    "                EntityLocation{ dst->Id, dci, dri, loc.Partition }\n",
)

replace_once(
'''        ++StructuralCounter;

        struct Move
''',
'''        struct Move
''')
replace_once(
'''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
''',
'''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
''')
replace_once(
    "            auto [dci, dri] = dst->AddRow(entity.Index);\n",
    "            auto [dci, dri] = dst->AddRow(entity.Index, loc.Partition);\n",
)
replace_once(
    "                EntityLocation{ dst->Id, dci, dri }\n",
    "                EntityLocation{ dst->Id, dci, dri, loc.Partition }\n",
)

replace_once(
'''    uint64_t StructuralVersion() const { return StructuralCounter; }
''',
'''    uint64_t StructuralVersion() const { return StructuralCounter; }

    uint64_t StructuralVersion(StoragePartitionId partition) const
    {
        return partition.Value < PartitionStructuralCounters.size()
            ? PartitionStructuralCounters[partition.Value]
            : 0;
    }
''')

replace_once(
'''    struct EntityChunkLocation
    {
        Chunk*   ChunkPtr = nullptr;
        uint32_t Row      = 0;
    };
''',
'''    struct EntityChunkLocation
    {
        Chunk*   ChunkPtr = nullptr;
        uint32_t Row      = 0;
        StoragePartitionId Partition = StoragePartitionId::Default();
    };
''')
replace_once(
'''        return { ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get(),
                 loc.RowIndex };
''',
'''        return { ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get(),
                 loc.RowIndex,
                 loc.Partition };
''')

replace_once(
'''    uint32_t FrameCounter = 0;
    uint64_t StructuralCounter = 0;
    bool     EntityCreated = false;
''',
'''    uint32_t FrameCounter = 0;
    uint64_t StructuralCounter = 0;
    std::vector<uint64_t> PartitionStructuralCounters{ 0 };
    std::vector<EntityPartitionMove> PartitionMoves;
    bool     EntityCreated = false;
''')

replace_once(
'''        StructuralCounter = other.StructuralCounter;
        EntityCreated = other.EntityCreated;
''',
'''        StructuralCounter = other.StructuralCounter;
        PartitionStructuralCounters = std::move(other.PartitionStructuralCounters);
        PartitionMoves = std::move(other.PartitionMoves);
        EntityCreated = other.EntityCreated;
''')

replace_once(
'''    uint32_t GenerationForIndex(EntityIndex index) const
    {
        return Entities.GenerationForIndex(index);
    }
''',
'''    uint32_t GenerationForIndex(EntityIndex index) const
    {
        return Entities.GenerationForIndex(index);
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
''')

WORLD.write_text(text)

# Restore the normal CI workflow after the one-shot patch and remove temporary
# automation so neither appears in the final implementation diff.
ci = Path(".github/workflows/ci.yml")
ci_text = ci.read_text()
ci_text = ci_text.replace(
'''      - name: Export temporary source snapshot
        run: tar --exclude=.git -czf sencha-source.tar.gz .

      - name: Upload temporary source snapshot
        uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a
        with:
          name: sencha-source
          path: sencha-source.tar.gz
          if-no-files-found: error
          retention-days: 1

''',
'',
)
ci.write_text(ci_text)

for temporary in (
    Path(".github/workflows/export-source.yml"),
    Path(".github/workflows/apply-partition-world-patch.yml"),
    Path("tools/temporary/apply_partition_world_patch.py"),
):
    if temporary.exists():
        temporary.unlink()
