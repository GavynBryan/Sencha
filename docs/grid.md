# Grid2d

`Grid2d<T>` is a fixed-size, row-major 2D array.  It owns a contiguous
`std::vector<T>` and contains all 2D-to-1D index arithmetic so that callers
never reimplement it.  The type carries zero game logic and zero render logic.

---

## Location

```
engine/include/math/spatial/Grid2d.h
```

```cpp
#include <math/spatial/Grid2d.h>
```

---

## Storage model

Cells are stored row-major: the full width of row 0 comes first, followed by
the full width of row 1, and so on.  A cell at `(col, row)` lives at flat
offset `row * width + col`.

Grid dimensions are fixed at construction.  There is no resize or reallocation
after the object is built.

---

## API

```cpp
// Construction
Grid2d<uint32_t> grid;                         // empty (0x0)
Grid2d<uint32_t> grid(width, height);          // zero-initialized
Grid2d<uint32_t> grid(width, height, fillId);  // filled with fillId

// Dimensions
uint32_t w   = grid.GetWidth();
uint32_t h   = grid.GetHeight();
size_t   n   = grid.Count();      // == w * h
bool     e   = grid.IsEmpty();    // == Count() == 0
bool     ok  = grid.InBounds(col, row);

// Cell access
uint32_t id  = grid.Get(col, row);
uint32_t& id = grid.Get(col, row);   // mutable ref
grid.Set(col, row, newId);
grid.Set(col, row, std::move(val));

// Flat span (for system sweeps — no index arithmetic in the loop)
std::span<const uint32_t> cells = grid.GetCells();
std::span<uint32_t>       cells = grid.GetCells();
```

Grid2d is copyable and movable.  Copy duplicates the cell vector.  Move leaves
the source in a 0x0 empty state.

---

## Idiomatic setup

### Standalone grid

Any code that needs a fixed 2D array can own a `Grid2d<T>` directly.

```cpp
// A 256x256 float heightmap, initialised to sea level.
Grid2d<float> heightmap(256, 256, 0.0f);
heightmap.Set(64, 32, 12.5f);
float h = heightmap.Get(64, 32);
```

### In a DataBatch

`Grid2d<T>` is move-constructible, so it can live inside a `DataBatch<T>`.

```cpp
struct TerrainChunk
{
    Grid2d<uint8_t> Biomes;
    Grid2d<float>   Heights;
};

DataBatch<TerrainChunk> chunks;
chunks.Emplace(TerrainChunk{ Grid2d<uint8_t>(64, 64), Grid2d<float>(64, 64, 0.0f) });
```

### Via Tilemap2d

`Tilemap2d` wraps a `Grid2d<uint32_t>` and is the primary consumer in the
engine.  Tile cells are accessed through the wrapper methods, which delegate
to `Grid2d`:

```cpp
DataBatch<Tilemap2d> maps;

const Transform2f origin = Transform2f::Identity();
DataBatchHandle<Tilemap2d> handle = maps.Emplace(
    world.Domain, origin, /*width=*/32u, /*height=*/32u, /*tileSize=*/16.0f);

Tilemap2d* map = maps.TryGet(handle.GetToken());
map->SetTile(0, 0, 1);   // tile ID 1 at column 0, row 0
map->SetTile(1, 0, 2);   // tile ID 2 at column 1, row 0

uint32_t id = map->GetTile(0, 0); // 1
```

### System sweep

Systems that need to process every cell iterate the flat span, not the 2D
accessor.  This keeps the hot loop a straight memory walk with no branch.

```cpp
// Pathfinding: count walkable cells.
std::span<const uint32_t> cells = grid.GetCells();
size_t walkable = 0;
for (uint32_t id : cells)
    if (id != 0) ++walkable;

// Serialisation: write all cells to a buffer in storage order.
std::span<const uint32_t> cells = grid.GetCells();
writer.WriteBytes(cells.data(), cells.size_bytes());
```

Reconstruction from a flat span is equally direct:

```cpp
Grid2d<uint32_t> grid(width, height);
std::span<uint32_t> cells = grid.GetCells();
reader.ReadBytes(cells.data(), cells.size_bytes());
```

---

## Tile ID convention (Tilemap2d)

When `Grid2d<uint32_t>` is used inside `Tilemap2d`, the engine treats cell
value `0` as the empty sentinel — no quad is emitted for it.  Tile IDs `1..N`
map to tileset index `N - 1`, laid out row-major in the spritesheet:

```
tileset index = tileId - 1
tileset col   = index % TilesetColumns
tileset row   = index / TilesetColumns
```

This convention is enforced by `TilemapRenderFeature`.  `Grid2d` itself has no
knowledge of it.

---

## Constraints

**Do not add render or visibility state to Grid2d.**  `Grid2d<T>` must remain a
pure data container.  Dirty flags, GPU buffer handles, visibility booleans, and
render-order hints belong in a separate struct (e.g. `TilemapRenderState`) that
is stored in its own `DataBatch` and references the grid via a `DataBatchKey`.

**Do not add game logic to Grid2d.**  Collision response, pathfinding graphs,
and AI hint layers are the concern of systems that read the grid as input data.
They must not be baked into the grid type itself.

**Do not store world position in Grid2d.**  The grid has no coordinate frame.
The position and orientation of the grid in the world is the concern of a
transform (e.g. `Tilemap2d::Handle`) owned by whoever wraps the grid.

**Column and row are unsigned.**  Attempting to call `Get` or `Set` with a
`(col, row)` pair where either component exceeds the grid dimensions is
undefined behaviour in release builds.  Call `InBounds(col, row)` first in any
path where the coordinates come from runtime input.

**Dimensions are fixed at construction.**  There is no `Resize`, `PushRow`, or
`PushColumn`.  If the size must change, construct a new `Grid2d` and replace
the old one.

**T must be move-constructible or copy-constructible.**  The internal
`std::vector<T>` requires one of these.  Most plain-data types satisfy both.

---

## Relationship to the tilemap pipeline

`Grid2d<T>` is the lowest layer.  The full 2D tilemap stack composes on top of
it without any of the layers reaching downward past their direct dependency:

```
Grid2d<uint32_t>          pure data: cells + dimensions
       │
Tilemap2d                 adds: transform slot, hierarchy registration, tile size
       │                  lives in: DataBatch<Tilemap2d>
       │
TilemapRenderState        adds: tileset texture, tileset layout, layer order
       │                  lives in: DataBatch<TilemapRenderState> (separate array)
       │
TilemapRenderFeature      sweeps DataBatch<TilemapRenderState>, resolves MapKey
                          → Tilemap2d, reads world transform, emits GpuTile instances
```

Maps and render states are intentionally in separate batches.  A map can exist
without a render state (purely logical, serialised, or off-screen).  A render
state can be disabled by removing it from its batch without touching the map
data.  Neither `Grid2d` nor `Tilemap2d` carries any render or visibility state.
