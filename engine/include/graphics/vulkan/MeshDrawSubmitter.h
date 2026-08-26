#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

//=============================================================================
// MeshDrawSubmitter
//
// Records an already-merged run list: for each run, the binds it needs that
// are not already in effect, then the indexed instanced draw.
//
// Deliberately narrow. Merge policy, per-view state, the instance stream, and
// push constants stay with the pass, because that is where the two callers
// genuinely differ -- the forward pass merges on material identity and pushes
// per draw, the shadow pass merges on mesh and section and pushes nothing.
// What they share is the bind-dedup state and the draw call, which they had
// written twice with different lifetimes: the forward pass in locals scoped to
// one call, the shadow pass in members reset per view.
//
// Dedup state describes one command buffer, and anything else that binds a
// pipeline or a vertex buffer to it invalidates that description. The shadow
// pass rebinds its instance stream at every view and has to say so; that rule
// now has one name instead of three assignments to remember.
//=============================================================================

// One indexed instanced draw, after the pass has decided everything about it.
struct MeshDrawCommand
{
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkBuffer VertexBuffer = VK_NULL_HANDLE;
    VkBuffer IndexBuffer = VK_NULL_HANDLE;
    std::uint32_t IndexCount = 0;
    std::uint32_t IndexOffset = 0;
    std::uint32_t InstanceCount = 0;
    // Offset into the instance stream bound at binding 1. Draws index their
    // instance data by queue position, so this is the run's first entry.
    std::uint32_t FirstInstance = 0;
};

// Which of a command's bindings are not already in effect.
struct MeshBindingDelta
{
    bool Pipeline = false;
    bool VertexBuffer = false;
    bool IndexBuffer = false;

    friend bool operator==(MeshBindingDelta, MeshBindingDelta) = default;
};

// What a command buffer currently has bound for mesh drawing.
class MeshBindState
{
public:
    // Reports what `draw` still needs bound and records it as bound. The
    // caller must emit exactly the binds the delta asks for; skipping one
    // would leave this description wrong for every later draw.
    [[nodiscard]] MeshBindingDelta Take(const MeshDrawCommand& draw)
    {
        const MeshBindingDelta delta{
            .Pipeline = draw.Pipeline != BoundPipeline,
            .VertexBuffer = draw.VertexBuffer != BoundVertexBuffer,
            .IndexBuffer = draw.IndexBuffer != BoundIndexBuffer,
        };
        BoundPipeline = draw.Pipeline;
        BoundVertexBuffer = draw.VertexBuffer;
        BoundIndexBuffer = draw.IndexBuffer;
        return delta;
    }

    // Forgets what is bound, so the next draw rebinds everything. Required
    // whenever something outside this state binds to the same command buffer.
    void Invalidate()
    {
        BoundPipeline = VK_NULL_HANDLE;
        BoundVertexBuffer = VK_NULL_HANDLE;
        BoundIndexBuffer = VK_NULL_HANDLE;
    }

private:
    VkPipeline BoundPipeline = VK_NULL_HANDLE;
    VkBuffer BoundVertexBuffer = VK_NULL_HANDLE;
    VkBuffer BoundIndexBuffer = VK_NULL_HANDLE;
};

// What a pass recorded, at the granularity the budget table asks about.
struct MeshDrawTally
{
    // Draw calls emitted, and the instances they cover. The pair is the
    // batching ratio: equal values mean nothing merged.
    std::uint32_t Draws = 0;
    std::uint32_t Instances = 0;
    std::uint32_t Triangles = 0;
    // Binds actually emitted, which is what the dedup is here to reduce.
    std::uint32_t PipelineBinds = 0;
    std::uint32_t VertexBufferBinds = 0;
    std::uint32_t IndexBufferBinds = 0;

    void Add(const MeshDrawCommand& draw, MeshBindingDelta delta)
    {
        ++Draws;
        Instances += draw.InstanceCount;
        Triangles += draw.IndexCount / 3u * draw.InstanceCount;
        PipelineBinds += delta.Pipeline ? 1u : 0u;
        VertexBufferBinds += delta.VertexBuffer ? 1u : 0u;
        IndexBufferBinds += delta.IndexBuffer ? 1u : 0u;
    }
};

class MeshDrawSubmitter
{
public:
    // Emits the binds `draw` needs and then the draw itself.
    void Submit(VkCommandBuffer cmd, const MeshDrawCommand& draw);

    // Forgets what is bound. Call after anything else binds to `cmd`, and at
    // the start of a command buffer.
    void Invalidate() { Binds.Invalidate(); }

    [[nodiscard]] const MeshDrawTally& Tally() const { return Counts; }
    void ClearTally() { Counts = MeshDrawTally{}; }

private:
    MeshBindState Binds;
    MeshDrawTally Counts;
};
