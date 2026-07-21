from pathlib import Path

world_path = Path("engine/include/ecs/World.h")
text = world_path.read_text()

old_duplicate = '''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
            assert(!src.Signature.test(id) && "Entity already has component.");
'''
new_duplicate = '''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
            assert(!src.Signature.test(id) && "Entity already has component.");
'''
if text.count(old_duplicate) != 1:
    raise RuntimeError("batch-add duplicate pattern changed unexpectedly")
text = text.replace(old_duplicate, new_duplicate, 1)

old_missing = '''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(src.Signature.test(id) && "Entity does not have component.");
'''
new_missing = '''            EntityLocation loc = Entities.GetLocation(entity);
            Archetype& src = *ArchetypeList[loc.ArchetypeId];
            assert(src.Chunks[loc.ChunkIndex]->Partition == loc.Partition);
            BumpStructural(loc.Partition);
            assert(src.Signature.test(id) && "Entity does not have component.");
'''
if text.count(old_missing) != 1:
    raise RuntimeError("batch-remove insertion pattern changed unexpectedly")
text = text.replace(old_missing, new_missing, 1)
world_path.write_text(text)

for temporary in (
    Path("tools/temporary/fix_partition_batch_paths.py"),
    Path(".github/workflows/fix-partition-batch-paths.yml"),
):
    temporary.unlink()
