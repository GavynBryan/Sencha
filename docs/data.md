# Data

The data layer is built around `DataBatch<T>` and `LifetimeHandle`.  `DataBatch<T>`
owns a dense contiguous array of T values; each item receives a stable
`DataBatchKey` so swap-and-pop removal never invalidates other handles.  Keys
pack a recyclable 20-bit index and a 12-bit generation — a stale key stops
resolving the moment its slot is removed and reused.  `LifetimeHandle` is the
RAII wrapper that calls `Detach` on the owning batch when the handle goes out
of scope, automatically removing the item without any manual bookkeeping.

---

## Location

```
Sencha.Core/include/core/batch/DataBatch.h
Sencha.Core/include/core/batch/DataBatchKey.h
Sencha.Core/include/core/raii/LifetimeHandle.h
Sencha.Core/include/core/raii/ILifetimeOwner.h
Sencha.Core/include/core/raii/DataBatchHandle.h
```

```cpp
#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <core/raii/LifetimeHandle.h>
#include <core/raii/DataBatchHandle.h>
```

---

## Core types

**`DataBatch<T>`** is the primary container.  It owns a `std::vector<T>` and a
parallel `IndexToKey` table; items are addressed by `DataBatchKey`, not by
pointer or integer index.  Removal swaps the target with the last element and
pops — O(1) and cache-dense.  `DataBatch<T>` implements `ILifetimeOwner` so
`DataBatchHandle<T>` can drive automatic removal through the RAII protocol.

**`DataBatchKey`** is a strongly-typed 32-bit value packing a 20-bit slot index
and a 12-bit generation.  The default-constructed value (`Value == 0`) is the
null sentinel.  A key becomes stale when its slot is deactivated; `TryGet` with
a stale key returns `nullptr` instead of a dangling pointer.

**`DataBatchBlock`** is lightweight metadata for a contiguous key range returned
by one `EmplaceBlock` call.  It carries `FirstKey` and `Count` and exposes
`KeyAt(i)` and `Contains(key)`.  It does **not** own any handles or keep items
alive.

**`ILifetimeOwner`** is the type-erased interface that connects handles to their
container.  Any class that manages resource lifetimes — batches, registries,
asset caches — implements `Attach(uint64_t)` and `Detach(uint64_t)`.

**`LifetimeHandle<T, KeyT>`** is the generic RAII handle.  `T` names the value
type; `KeyT` names the token shape (pointer for `InstanceRegistry`, value type
for `DataBatch`).  Construction calls `Attach`; destruction and `Reset()` call
`Detach`.  Move transfers ownership; copy is deleted.

**`DataBatchHandle<T>`** is the alias `LifetimeHandle<T, DataBatchKey>`.  It is
what `DataBatch<T>::Emplace` returns and what game code holds.

---

## API

```cpp
// ---- Single-item emplacement ---------------------------------------------

// Emplace constructs T in-place and returns an owning RAII handle.
DataBatch<Particle> particles;
DataBatchHandle<Particle> handle = particles.Emplace(x, y, z);

// EmplaceUnowned emplace without creating a handle. Caller is responsible
// for removal. Useful inside TransformStore or similar dual-batch wrappers.
DataBatchKey key = particles.EmplaceUnowned(x, y, z);

// ---- Block emplacement ---------------------------------------------------

// Allocate count items in one operation; factory(index) returns each T.
DataBatchBlock block = particles.EmplaceBlock(4, [](size_t i) {
    return Particle{ float(i), 0.0f, 0.0f };
});
// block.FirstKey, block.Count, block.KeyAt(i), block.Contains(key)

// ---- Random access -------------------------------------------------------

T*       ptr = particles.TryGet(handle);  // nullptr if stale
T*       ptr = particles.TryGet(key);
const T* ptr = particles.TryGet(key);     // const overload

bool exists = particles.Contains(key);
uint32_t i  = particles.IndexOf(key);     // UINT32_MAX if absent

// ---- Removal -------------------------------------------------------------

// Implicit: handle destructs → Detach → swap-and-pop.
{
    auto h = particles.Emplace(1.0f, 0.0f, 0.0f);
}  // item removed here

// Explicit early release:
handle.Reset();         // Detach + nullify; handle.IsValid() == false

// Bulk removal:
particles.RemoveKey(key);
particles.RemoveBlock(block);
particles.RemoveKeys(std::span<const DataBatchKey>{ keys });
particles.RemoveHandles(std::span<DataBatchHandle<T>>{ handles });

// ---- Iteration -----------------------------------------------------------

// The whole point of DOD: one contiguous span.
for (Particle& p : particles.GetItems())  { /* ... */ }
for (const Particle& p : particles)       { /* range-for */ }
size_t n = particles.Count();

// ---- Version and dirty tracking -----------------------------------------

uint64_t v = particles.GetVersion();  // bumped on any structural change
bool dirty = particles.CheckAndClearDirty();  // test-and-clear

// Sort the dense array if dirty; updates internal key→index tables.
particles.SortIfDirty([](const Particle& a, const Particle& b) {
    return a.Z < b.Z;
});

// ---- RAII handle interface -----------------------------------------------

bool valid      = handle.IsValid();
DataBatchKey k  = handle.GetToken();
if (handle) { /* ... */ }           // explicit bool conversion
```

---

## Idiomatic setup

### Single item with RAII

```cpp
DataBatch<Particle> particles;

// Emplace returns a handle; the item lives exactly as long as the handle.
auto handle = particles.Emplace(100.0f, 200.0f, 0.0f);

// Access by handle without extracting the key.
if (Particle* p = particles.TryGet(handle))
    p->X += 10.0f;

// Item is removed when handle goes out of scope or is reset explicitly.
handle.Reset();
```

### Block allocation

Use `EmplaceBlock` when adding a batch of items whose lifetimes are coupled and
managed together (e.g. all tiles in a tilemap layer).  `DataBatchBlock` is a
plain-old-data range descriptor — store it alongside whichever object owns the
logical group, and call `RemoveBlock` when that object is torn down.

```cpp
DataBatch<GpuTile> tiles;

DataBatchBlock block = tiles.EmplaceBlock(width * height, [&](size_t i) {
    return GpuTile{ tileIds[i] };
});

// Address a specific tile by local offset:
for (size_t i = 0; i < block.Count; ++i)
{
    DataBatchKey key = block.KeyAt(i);
    if (GpuTile* tile = tiles.TryGet(key))
        tile->Flags = 1;
}

// Remove the whole block at once:
tiles.RemoveBlock(block);
```

### Move semantics

```cpp
DataBatchHandle<Particle> a = particles.Emplace(1.0f, 2.0f, 3.0f);

// Transfer ownership — a becomes invalid, b owns the item.
DataBatchHandle<Particle> b = std::move(a);
assert(!a.IsValid());
assert( b.IsValid());

// Move-assign: b's current item is removed, then b takes ownership of c's.
DataBatchHandle<Particle> c = particles.Emplace(4.0f, 5.0f, 6.0f);
b = std::move(c);
// The item b previously owned is gone; c is now invalid.
```

### Implementing ILifetimeOwner for multi-resource handles

When a single logical entity spans two or more batches, implement
`ILifetimeOwner` directly and return a single `DataBatchHandle` whose
destruction removes from all batches at once.

```cpp
class EntityStore : public ILifetimeOwner, public IService
{
public:
    DataBatchHandle<Position> Emplace(Position pos, Velocity vel)
    {
        const DataBatchKey key = Positions.EmplaceUnowned(pos);
        try   { Velocities.EmplaceAtKey(key, vel); }
        catch (...) { Positions.RemoveKey(key); throw; }
        return DataBatchHandle<Position>(this, key);
    }

    void Attach(uint64_t) override {}

    void Detach(uint64_t token) override
    {
        DataBatchKey key{};
        std::memcpy(&key, &token, sizeof(key));
        Velocities.RemoveKey(key);
        Positions.RemoveKey(key);
    }

    DataBatch<Position> Positions;
    DataBatch<Velocity> Velocities;
};
```

### EmplaceUnowned and manual removal

`EmplaceUnowned` returns a raw key with no handle.  Use it inside owner
objects that manage their own lifetime accounting (like `EntityStore` above or
`TransformStore`) and do not want an extra RAII level.

```cpp
DataBatchKey key = particles.EmplaceUnowned(x, y, z);
// ... later, explicit removal required:
particles.RemoveKey(key);
```

---

## Constraints

**Do not copy `DataBatchHandle`.**  Copy constructor and copy-assignment are
deleted.  Two owners for the same item would double-free it on destruction.
Transfer ownership with `std::move` or keep the handle in the object that
logically owns the item.

**Do not hold a raw `DataBatchKey` as a long-lived ownership token.**  A key
does not keep the item alive; it is a look-up token only.  If you need
ownership semantics, use a `DataBatchHandle`.  Raw keys are appropriate for
non-owning references — render features reading transforms they did not create.

**Do not assume a raw key is valid without calling `TryGet` or `Contains`.**
Any `RemoveKey` or handle destruction can deactivate a slot.  The generation
check in `TryGet` returns `nullptr` rather than a dangling pointer, but the
check only fires if you actually call it.

**`DataBatchBlock` does not own or keep items alive.**  It is metadata.  If you
call `RemoveBlock` or destroy the handles that were not created for those keys,
the block's `KeyAt` entries become stale silently.  Store blocks only alongside
the code responsible for the matching `RemoveBlock` call.

**`EmplaceUnowned` items must be removed explicitly.**  The batch has no record
that these items are unmanaged; they will not be cleaned up when the batch is
destroyed (they are destroyed with the batch, but the slot is never `Detach`-ed
by a handle).  Use `EmplaceUnowned` only inside types that implement
`ILifetimeOwner` and guarantee `RemoveKey` is called through `Detach`.

**Do not pass `Value == 0` as a `DataBatchKey`.**  Zero is the null sentinel.
`DataBatch` will silently ignore a remove-by-null-key and `TryGet` will return
`nullptr`.

**`EmplaceAtKey` requires the target slot to be free.**  It throws
`std::invalid_argument` if the slot is occupied or the generation does not
match.  It is intended for paired stores (like `TransformStore`) that must
place world and local transforms at the same key index.

**Do not rely on dense-array indices surviving a removal.**  Swap-and-pop moves
the last element into the removed slot.  Any externally cached integer index is
invalidated.  Use `DataBatchKey` or `DataBatchHandle` for stable addressing;
use `GetItems()` for iteration-only passes where order does not matter.

---

## Relationship to the rest of the engine

`DataBatch` is the shared data substrate.  Systems read `GetItems()` spans each
tick; render features hold non-owning pointers to the game's batches.  The RAII
chain ensures items are always removed when the logical owner is torn down,
regardless of the order in which game objects are destroyed.

```
Game / scene code
       │  holds DataBatchHandle<T>  (RAII owner)
       │  handle destructs → Detach → swap-and-pop → item gone
       │
DataBatch<T>                implements ILifetimeOwner
       │  dense Items[]  ←  iterated by systems and render features
       │  generational KeySlots[]  ←  TryGet resolves stable handles
       │
Systems (ECS-style loops)
       │  for (auto& item : batch.GetItems()) { … }
       │
TransformStore / AssetCache
       │  store non-owning DataBatch* pointers
       │  read Items[] directly; never remove
       │
LifetimeHandle<T, KeyT>
       │  generic base; DataBatchHandle<T> is the DataBatch alias
       │  InstanceRegistryHandle<T> is the pointer-registry alias
       │  AssetCache uses NoAttach tag to avoid double-counting refcount
```
