#include <render/extract/DebugLineExtraction.h>

#include <debug/DebugLines.h>
#include <ecs/World.h>

namespace
{
    void Append(std::span<const DebugLine> lines, std::vector<ColoredVertex>& out)
    {
        for (const DebugLine& line : lines)
        {
            out.push_back(ColoredVertex{ line.From, line.Color });
            out.push_back(ColoredVertex{ line.To, line.Color });
        }
    }
}

void ExtractDebugLines(const World& world, DebugLineFrame& out)
{
    const DebugLineChannels* channels = world.TryGetResource<DebugLineChannels>();
    if (channels == nullptr)
        return;
    for (const DebugLineList& list : channels->All())
    {
        Append(list.Tested(), out.Tested);
        Append(list.OnTop(), out.OnTop);
    }
}
