#include <graphics/vulkan/MeshDrawSubmitter.h>

void MeshDrawSubmitter::Submit(VkCommandBuffer cmd, const MeshDrawCommand& draw)
{
    const MeshBindingDelta delta = Binds.Take(draw);

    if (delta.Pipeline)
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.Pipeline);
    if (delta.VertexBuffer)
    {
        // Binding 0 is the mesh's own vertex stream. Binding 1 is the pass's
        // per-instance stream, which outlives a run and is bound by the pass.
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &draw.VertexBuffer, &offset);
    }
    if (delta.IndexBuffer)
        vkCmdBindIndexBuffer(cmd, draw.IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, draw.IndexCount, draw.InstanceCount, draw.IndexOffset, 0,
                     draw.FirstInstance);
    Counts.Add(draw, delta);
}
