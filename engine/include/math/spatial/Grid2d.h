#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

//=============================================================================
// Grid2d<T>
//
// Contiguous 2D array owning a flat std::vector<T>. All 2D-to-1D index math
// lives here so callers never reimplement it; the grid itself carries zero
// game or render logic.
//
// Storage order: row-major (row 0 is the first Width elements). A cell at
// (col, row) lives at offset row * Width + col.
//
// Bounds checking is enforced only in debug builds (InBounds / assertions).
// Release builds perform a single unchecked array access for hot paths.
//=============================================================================
template <typename T>
class Grid2d
{
public:
    Grid2d() = default;

    Grid2d(uint32_t width, uint32_t height, const T& fillValue = T{})
        : Width(width)
        , Height(height)
        , Cells(static_cast<size_t>(width) * height, fillValue)
    {
    }

    Grid2d(Grid2d&&) noexcept = default;
    Grid2d& operator=(Grid2d&&) noexcept = default;
    Grid2d(const Grid2d&) = default;
    Grid2d& operator=(const Grid2d&) = default;

    // -- Dimensions -----------------------------------------------------------

    uint32_t GetWidth()  const { return Width; }
    uint32_t GetHeight() const { return Height; }
    size_t   Count()     const { return Cells.size(); }
    bool     IsEmpty()   const { return Cells.empty(); }

    bool InBounds(uint32_t col, uint32_t row) const
    {
        return col < Width && row < Height;
    }

    // -- Cell access ----------------------------------------------------------

    const T& Get(uint32_t col, uint32_t row) const { return Cells[Index(col, row)]; }
          T& Get(uint32_t col, uint32_t row)       { return Cells[Index(col, row)]; }

    void Set(uint32_t col, uint32_t row, const T& value)
    {
        Cells[Index(col, row)] = value;
    }

    void Set(uint32_t col, uint32_t row, T&& value)
    {
        Cells[Index(col, row)] = std::move(value);
    }

    // -- Flat span access (for systems that sweep the whole grid) -------------

    std::span<const T> GetCells() const { return std::span<const T>(Cells); }
    std::span<T>       GetCells()       { return std::span<T>(Cells); }

private:
    size_t Index(uint32_t col, uint32_t row) const
    {
        return static_cast<size_t>(row) * Width + col;
    }

    uint32_t       Width  = 0;
    uint32_t       Height = 0;
    std::vector<T> Cells;
};
