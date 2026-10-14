#pragma once

#include <graphics/vulkan/ImmediatePipeline.h>
#include <render/ColoredVertex.h>

#include <math/Mat.h>

// One placement of an instanced colored-vertex draw: the model matrix by rows,
// exactly as the instanced shader's four per-instance attributes read it.
struct ColoredVertexInstance
{
    Mat4 Model = Mat4::Identity();
};

// ColoredVertex in, flat colour out, under the ViewProjection push constant.
// Callers change topology, blending, culling and depth writes on the result.
[[nodiscard]] ImmediatePipelineConfig ColoredVertexConfig(VkPrimitiveTopology topology);

// As above, drawn once per ColoredVertexInstance in binding 1.
[[nodiscard]] ImmediatePipelineConfig InstancedColoredVertexConfig(VkPrimitiveTopology topology);

using ColoredVertexPipeline = ImmediatePipeline<ColoredVertex>;
