from pathlib import Path

world_path = Path("engine/include/ecs/World.h")
world = world_path.read_text()
old = '''    StoragePartitionId GetEntityPartition(EntityId entity) const
    {
        const EntityLocation loc = Entities.GetLocation(entity);
        const Chunk* chunk = ArchetypeList[loc.ArchetypeId]->Chunks[loc.ChunkIndex].get();
        assert(chunk->Partition == loc.Partition);
        return loc.Partition;
    }

'''
new = old + '''    // Reconstructs the live generational id for a raw index obtained from an
    // active chunk query. Query rows are alive by construction; callers outside
    // query storage should continue to hold EntityId directly.
    EntityId ResolveEntityIndex(EntityIndex index) const
    {
        const EntityId entity{ index, Entities.GenerationForIndex(index) };
        assert(Entities.IsAlive(entity));
        return entity;
    }

'''
if world.count(old) != 1:
    raise RuntimeError("World entity-partition block changed unexpectedly")
world_path.write_text(world.replace(old, new, 1))

query_path = Path("engine/include/ecs/Query.h")
query = query_path.read_text()
old = '''    Chunk*                     RawChunk = nullptr;
    uint32_t                   Frame    = 0;
    std::array<uint32_t, NAcc> ColIndices{};

    const EntityIndex* Entities() const { return RawChunk->EntityIndices(); }
    uint32_t           Count()    const { return RawChunk->RowCount; }
    StoragePartitionId Partition() const { return RawChunk->Partition; }
'''
new = '''    Chunk*                     RawChunk = nullptr;
    const World*               Owner    = nullptr;
    uint32_t                   Frame    = 0;
    std::array<uint32_t, NAcc> ColIndices{};

    const EntityIndex* Entities() const { return RawChunk->EntityIndices(); }
    EntityId Entity(uint32_t row) const
    {
        assert(Owner != nullptr && row < Count());
        return Owner->ResolveEntityIndex(Entities()[row]);
    }
    uint32_t           Count()    const { return RawChunk->RowCount; }
    StoragePartitionId Partition() const { return RawChunk->Partition; }
'''
if query.count(old) != 1:
    raise RuntimeError("ChunkView identity block changed unexpectedly")
query = query.replace(old, new, 1)
old = '''            ChunkView<Accessors...> view;
            view.Frame = frame;
'''
new = '''            ChunkView<Accessors...> view;
            view.Owner = W;
            view.Frame = frame;
'''
if query.count(old) != 1:
    raise RuntimeError("Query view construction changed unexpectedly")
query_path.write_text(query.replace(old, new, 1))

for cleanup in (
    Path("tools/temporary/add_query_entity_identity.py"),
    Path(".github/workflows/add-query-entity-identity.yml"),
):
    cleanup.unlink()
