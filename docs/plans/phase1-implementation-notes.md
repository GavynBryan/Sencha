# Unified runtime world Phase 1 implementation notes

**Superseded.** This described partition-capable ECS storage sitting *beneath* the
per-registry runtime, with existing callers still on the default partition. The
cutover since removed the per-registry runtime entirely: `RuntimeWorld` owns one
`World` and zones are storage partitions in it. See
[`unified-runtime-world.md`](unified-runtime-world.md) for the model and
[`unified-world-hardening.md`](unified-world-hardening.md) for the costs that move
created and how they were closed.

Kept because the storage mechanisms it introduced — explicit partitions, entity
migration, partition destruction, structural versions, migration journals — are all
still live and this is where their first tests came from.
