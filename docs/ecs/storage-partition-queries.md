# Storage-partition query filtering

Status: live. The mechanism behind domain participation in the unified runtime
world — `FrameZoneView` builds the Visible/Physics/Logic/Audio/Resident sets and
every frame domain iterates through the filtered path below.

`StoragePartitionSet` is a dense membership table over 16-bit
`StoragePartitionId` values. It combines:

- a packed word table for O(1) membership tests;
- an insertion-ordered member list for deterministic domain iteration;
- retained capacity across `Clear()` so frame-owned sets can be rebuilt without
  allocation churn after reaching their steady resident-slot range.

## Query paths

Cached `Query` objects expose two paths:

```cpp
query.ForEachChunk(callback);
query.ForEachChunkIn(activePartitions, callback);
```

The original unfiltered path instantiates an implementation with partition
filtering disabled through `if constexpr`. It therefore gains no runtime
membership branch.

The filtered path checks `activePartitions.Contains(chunk.Partition)` once per
non-empty chunk before evaluating `Changed<T>` filters or invoking the callback.
It never checks partition membership per entity.

A single cached query can serve several frame domains because the partition set
is supplied per invocation rather than captured by the query:

```cpp
logicQuery.ForEachChunkIn(frame.Logic, runLogic);
logicQuery.ForEachChunkIn(frame.Audio, inspectAudioRelevantLogic);
```

Archetype matching remains cached exactly as before. Partition changes and
participation changes do not create archetypes or force query cache rebuilds.

## Change tracking

`Write<T>` bumps column versions only for chunks that pass the partition filter.
`Changed<T>` and partition filtering compose: an inactive changed chunk is not
visited, and an active unchanged chunk is not visited.

## Ordering caveat

`Members()` is insertion-ordered, and partition ids are recycled: `RuntimeWorld`
hands out the most recently freed slot first, so which id a zone receives depends
on the load/unload history that preceded it. Membership tests and per-chunk work
are unaffected, but an accumulation that depends on the order partitions are
visited in is not reproducible across differing streaming histories. Order by
something authored — `ZoneId` — when the result has to be stable.
