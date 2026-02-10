#pragma once

#include <service/IService.h>
#include <math/Vec.h>
#include <vector>
#include <random>
#include <algorithm>
#include <cassert>
#include <array>

//=============================================================================
// Maze
//
// Service that holds a grid-based maze. Each cell is either a wall or a
// passage. The maze is generated using a randomized depth-first search
// (recursive backtracker) algorithm.
//
// Grid coordinates: (row, col) where each cell occupies a 1x1 world-space
// tile centered at (col + 0.5, 0, row + 0.5). Walls are solid blocks
// that extend from floor to ceiling.
//
// The outer border is always walls. The generator carves passages through
// the interior, producing a perfect maze (exactly one path between any
// two open cells).
//=============================================================================
class Maze : public IService
{
public:
	enum Cell : uint8_t
	{
		Wall    = 0,
		Passage = 1
	};

	Maze(int width, int height, uint32_t seed = 42)
		: Width(width)
		, Height(height)
	{
		assert(width >= 5 && height >= 5 && "Maze must be at least 5x5.");
		assert(width % 2 == 1 && height % 2 == 1 && "Maze dimensions must be odd.");

		Grid.resize(static_cast<size_t>(width) * height, Wall);
		Generate(seed);
	}

	Cell GetCell(int row, int col) const
	{
		if (row < 0 || row >= Height || col < 0 || col >= Width)
		{
			return Wall;
		}
		return Grid[static_cast<size_t>(row) * Width + col];
	}

	bool IsWall(int row, int col) const { return GetCell(row, col) == Wall; }
	bool IsPassage(int row, int col) const { return GetCell(row, col) == Passage; }

	int GetWidth() const { return Width; }
	int GetHeight() const { return Height; }

	Vec3 GetSpawnPosition() const
	{
		return Vec3(1.5f, 0.5f, 1.5f);
	}

private:
	void SetCell(int row, int col, Cell value)
	{
		Grid[static_cast<size_t>(row) * Width + col] = value;
	}

	void Generate(uint32_t seed)
	{
		std::mt19937 rng(seed);

		// Start carving from (1,1)
		SetCell(1, 1, Passage);

		struct Frame
		{
			int Row;
			int Col;
		};

		std::vector<Frame> stack;
		stack.push_back({1, 1});

		// Direction offsets: up, down, left, right (step of 2)
		static constexpr std::array<std::pair<int, int>, 4> Directions = {{
			{-2, 0}, {2, 0}, {0, -2}, {0, 2}
		}};

		while (!stack.empty())
		{
			auto& current = stack.back();
			int r = current.Row;
			int c = current.Col;

			// Collect unvisited neighbors
			std::array<int, 4> neighbors;
			int count = 0;

			for (int d = 0; d < 4; ++d)
			{
				int nr = r + Directions[d].first;
				int nc = c + Directions[d].second;
				if (nr > 0 && nr < Height - 1 && nc > 0 && nc < Width - 1
					&& IsWall(nr, nc))
				{
					neighbors[count++] = d;
				}
			}

			if (count == 0)
			{
				stack.pop_back();
				continue;
			}

			// Pick a random unvisited neighbor
			int pick = static_cast<int>(rng() % count);
			int d = neighbors[pick];
			int nr = r + Directions[d].first;
			int nc = c + Directions[d].second;

			// Carve the wall between current and neighbor
			SetCell(r + Directions[d].first / 2, c + Directions[d].second / 2, Passage);
			SetCell(nr, nc, Passage);

			stack.push_back({nr, nc});
		}
	}

	int Width;
	int Height;
	std::vector<Cell> Grid;
};
