#pragma once

#include <cstdint>
#include <vector>

template<typename T>
struct Grid3d
{
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Depth = 0;
    std::vector<T> Cells;

    bool IsEmpty() const { return Cells.empty(); }
    uint32_t Count() const { return Width * Height * Depth; }

    bool InBounds(uint32_t col, uint32_t row, uint32_t layer) const
    {
        return col < Width && row < Height && layer < Depth;
    }

    T& Get(uint32_t col, uint32_t row, uint32_t layer)
    {
        return Cells[Index(col, row, layer)];
    }

    const T& Get(uint32_t col, uint32_t row, uint32_t layer) const
    {
        return Cells[Index(col, row, layer)];
    }

    void Set(uint32_t col, uint32_t row, uint32_t layer, const T& value)
    {
        Cells[Index(col, row, layer)] = value;
    }

private:
    size_t Index(uint32_t col, uint32_t row, uint32_t layer) const
    {
        return static_cast<size_t>(layer) * Width * Height
             + static_cast<size_t>(row) * Width
             + col;
    }
};
