#pragma once

#include <render/ColoredVertex.h>

#include <vector>

class World;

// The frame's debug lines in render form, two vertices per line.
struct DebugLineFrame
{
    std::vector<ColoredVertex> Tested;
    std::vector<ColoredVertex> OnTop;

    void Clear()
    {
        Tested.clear();
        OnTop.clear();
    }
};

// Every channel of the World's DebugLineChannels, appended to `out`.
void ExtractDebugLines(const World& world, DebugLineFrame& out);
