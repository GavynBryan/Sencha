#include <graphics/vulkan/ColoredVertexPipelines.h>

#include <shaders/kColoredFragSpv.h>
#include <shaders/kColoredInstancedVertSpv.h>
#include <shaders/kColoredVertSpv.h>

#include <cstddef>

static_assert(sizeof(ColoredVertexInstance) == 64);

ImmediatePipelineConfig ColoredVertexConfig(VkPrimitiveTopology topology)
{
    ImmediatePipelineConfig config;
    config.VertexSpirv = kColoredVertSpv;
    config.VertexWordCount = kColoredVertSpvWordCount;
    config.VertexName = "Colored vertex";
    config.FragmentSpirv = kColoredFragSpv;
    config.FragmentWordCount = kColoredFragSpvWordCount;
    config.FragmentName = "Colored fragment";
    config.Topology = topology;
    config.Attributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(ColoredVertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(ColoredVertex, Color) },
    };
    return config;
}

ImmediatePipelineConfig InstancedColoredVertexConfig(VkPrimitiveTopology topology)
{
    ImmediatePipelineConfig config = ColoredVertexConfig(topology);
    config.VertexSpirv = kColoredInstancedVertSpv;
    config.VertexWordCount = kColoredInstancedVertSpvWordCount;
    config.VertexName = "Instanced colored vertex";
    config.InstanceStride = sizeof(ColoredVertexInstance);
    config.Attributes.push_back({ 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 });
    config.Attributes.push_back({ 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 });
    config.Attributes.push_back({ 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 });
    config.Attributes.push_back({ 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 });
    return config;
}
