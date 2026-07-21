# Unified runtime world Phase 1 implementation notes

This branch implements partition-capable ECS storage beneath the current per-registry runtime. Existing callers remain on the default partition while new tests exercise explicit partitions, entity migration, partition destruction, structural versions, and migration journals.
