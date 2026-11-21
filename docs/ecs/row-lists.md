# Row lists

A component that holds a list whose length varies from entity to entity keeps
the list off its row, in a `ListPool` (`core/containers/ListPool.h`) held as a
World resource. The row holds a handle and whatever it needs without the list
(a count, the next id to mint). An entity pays for what its list holds, not for
the longest list any entity may have.

Two components use it: `AbilityGrants` (`AbilityGrantLists`) and
`EquipmentState` (`EquipmentRecordLists`).

## Why not a fixed array on the row

A fixed array costs every row its capacity. Sixteen grants cost 520 bytes on an
actor granted one ability, and a body that wears twenty-five things did not fit
at all. Raising the number raises what every row pays.

## Ownership

- **A row references its list.** The component's `ComponentTraits` hooks retain
  the list a row names when the component is added and release it when the
  component is removed or its entity destroyed. A list nothing references is
  freed. `World::ReplaceComponent` runs the new value's `OnAdd` before the old
  value's `OnRemove`, so replacing a row with one naming the same list keeps it.
- **A row writes only a list of its own.** `ListPool::Edit` makes a list for a
  row that names none and copies one that another row also names, so two rows
  holding the same handle (a component value copied onto a second entity) never
  write through to each other.
- **A value built outside a row is unreferenced until it is placed.**
  `ListPool::Make` returns a list no row references. Adding or replacing the
  component retains it. One that is never placed (a load that failed after
  building its value) is freed by `DropUnreferenced`, which the ability pass and
  the equipment pass call each tick.
- **Freed lists are kept for reuse** up to `SpareRetention`'s limit (the larger
  of 64 and a quarter of those live), with the storage they grew; the rest go
  back to the allocator. A warm pool allocates nothing.

## What may not be done with such a component

- **Do not assign over a live row.** `*world.TryGet<T>(e) = value` bypasses the
  hooks: the old list is never released and the new one never retained. Use the
  component's own calls (`SetAbilityGrants`, `SetEquipment`) or
  `ReplaceComponent`.
- **Do not keep the row's bytes as its value.** A copy of the row is a handle,
  not the list. Code that keeps a value across time (an undo command, a
  snapshot being decoded, a scene being read) keeps the by-value form
  (`AbilityGrantList`, `EquipmentRecordList`) and writes it back through the
  component's calls. Kyusu's generic commands already go through the
  serializer, which does.
- **`InitializeComponent` assumes the row held nothing**, as it does for a
  component that owns an asset: loading over a live value leaks the old list.

## Limits

The pool has no budget: it grows with the lists live in the World and reports
`Stats()` (live, spare, peak, unreferenced, allocations, reuses). What bounds a
list is its component's own rule: `kAbilityGrantCapacity` for grants
(`docs/gameplay/abilitykit.md`, "Grants and input"), a layout's slots for worn
records (`docs/gameplay/equipment.md`, "Capacities").
`test/core/ListPoolTests.cpp` pins the reference rules and the retention limit
at its boundary.
