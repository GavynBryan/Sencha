#include <render/features/SpriteFeature.h>

#include <render/backend/vulkan/VulkanBufferService.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanFrameScratch.h>
#include <render/backend/vulkan/VulkanPipelineCache.h>
#include <render/backend/vulkan/VulkanShaderCache.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    // Six verts, two triangles, generated entirely from gl_VertexIndex via
    // instance data. The fragment shader samples the bindless image array
    // by the per-instance index and multiplies by the per-instance color.
    constexpr const char* kSpriteVertexGlsl = R"GLSL(
#version 450

layout(set = 0, binding = 0) uniform FrameUbo {
    vec2 InvViewport;
    vec2 _pad;
} uFrame;

layout(location = 0) in vec2 iCenter;
layout(location = 1) in vec2 iHalfExtents;
layout(location = 2) in vec2 iUvMin;
layout(location = 3) in vec2 iUvMax;
layout(location = 4) in uint iColor;
layout(location = 5) in uint iTextureIndex;
layout(location = 6) in vec2 iSinCos;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) flat out uint vTextureIndex;

void main() {
    // Two triangles covering a unit-centered quad in [-1, +1].
    const vec2 kCorner[6] = vec2[](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
        vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
    );
    const vec2 kCornerUv[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );

    vec2 local = kCorner[gl_VertexIndex] * iHalfExtents;
    float s = iSinCos.x;
    float c = iSinCos.y;
    vec2 rotated = vec2(local.x * c - local.y * s,
                        local.x * s + local.y * c);
    vec2 pixels = iCenter + rotated;

    // Screen-pixel to NDC. Origin is top-left, so we flip Y.
    vec2 ndc = pixels * uFrame.InvViewport * 2.0 - 1.0;
    ndc.y = -ndc.y;

    gl_Position = vec4(ndc, 0.0, 1.0);
    vUv = mix(iUvMin, iUvMax, kCornerUv[gl_VertexIndex]);
    vColor = unpackUnorm4x8(iColor);
    vTextureIndex = iTextureIndex;
}
)GLSL";

    constexpr const char* kSpriteFragmentGlsl = R"GLSL(
#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 1) uniform sampler2D uImages[];

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) flat in uint vTextureIndex;

layout(location = 0) out vec4 oColor;

void main() {
    vec4 texel = texture(uImages[nonuniformEXT(vTextureIndex)], vUv);
    oColor = texel * vColor;
}
)GLSL";
}

void SpriteFeature::Setup(const RendererServices& services)
{
    Logging = services.Logging;
    DeviceService = services.Device;
    Buffers = services.Buffers;
    Shaders = services.Shaders;
    Pipelines = services.Pipelines;
    Descriptors = services.Descriptors;
    Scratch = services.Scratch;

    if (Logging == nullptr || DeviceService == nullptr || Buffers == nullptr
        || Shaders == nullptr || Pipelines == nullptr || Descriptors == nullptr
        || Scratch == nullptr)
    {
        return;
    }

    Logger& log = Logging->GetLogger<SpriteFeature>();

    VertexShader = Shaders->CompileFromSource(
        kSpriteVertexGlsl, ShaderStage::Vertex, "SpriteFeature.vert");
    FragmentShader = Shaders->CompileFromSource(
        kSpriteFragmentGlsl, ShaderStage::Fragment, "SpriteFeature.frag");

    if (!VertexShader.IsValid() || !FragmentShader.IsValid())
    {
        log.Error("SpriteFeature: shader compile failed");
        return;
    }

    // No push constants; everything flows through set 0 (frame UBO + bindless).
    PipelineLayout = Descriptors->GetDefaultPipelineLayout();
    if (PipelineLayout == VK_NULL_HANDLE)
    {
        log.Error("SpriteFeature: failed to acquire pipeline layout");
        return;
    }

    // Point descriptor binding 0 at the scratch ring. SpriteFeature owns
    // this wiring for now because it's currently the only frame-UBO consumer.
    // When a second feature wants the frame UBO we'll promote this to a
    // shared setup step somewhere above the feature layer.
    //
    // Range is a conservative cap that must cover the biggest frame UBO any
    // feature reaches via dynamic offset -- 256 bytes is comfortably under
    // every device's maxUniformBufferRange and big enough for anything a
    // near-term feature would put here.
    Descriptors->SetFrameUniformBuffer(Scratch->GetBuffer(), 256);

    Valid = true;
}

void SpriteFeature::Teardown()
{
    if (Shaders != nullptr)
    {
        if (VertexShader.IsValid())   Shaders->Destroy(VertexShader);
        if (FragmentShader.IsValid()) Shaders->Destroy(FragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    CachedPipeline = VK_NULL_HANDLE;
    CachedColorFormat = VK_FORMAT_UNDEFINED;
    PipelineLayout = VK_NULL_HANDLE;
    Pending.clear();
    Pending.shrink_to_fit();
    Valid = false;
}

void SpriteFeature::Submit(const Sprite& sprite)
{
    if (!Valid) return;
    Pending.push_back(sprite);
}

void SpriteFeature::ClearPending()
{
    Pending.clear();
}

bool SpriteFeature::BuildPipeline(VkFormat colorFormat)
{
    if (CachedPipeline != VK_NULL_HANDLE && CachedColorFormat == colorFormat)
    {
        return true;
    }

    GraphicsPipelineDesc desc{};
    desc.VertexShader = VertexShader;
    desc.FragmentShader = FragmentShader;
    desc.Layout = PipelineLayout;

    // One binding, instance-rate, stride = sizeof(GpuInstance).
    VertexInputBindingDesc binding{};
    binding.Binding = 0;
    binding.Stride = sizeof(GpuInstance);
    binding.InputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    desc.VertexBindings.push_back(binding);

    auto addAttr = [&](uint32_t location, VkFormat format, uint32_t offset)
    {
        VertexInputAttributeDesc a{};
        a.Location = location;
        a.Binding = 0;
        a.Format = format;
        a.Offset = offset;
        desc.VertexAttributes.push_back(a);
    };
    addAttr(0, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuInstance, Center));
    addAttr(1, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuInstance, HalfExtents));
    addAttr(2, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuInstance, UvMin));
    addAttr(3, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuInstance, UvMax));
    addAttr(4, VK_FORMAT_R32_UINT,      offsetof(GpuInstance, Color));
    addAttr(5, VK_FORMAT_R32_UINT,      offsetof(GpuInstance, TextureIndex));
    addAttr(6, VK_FORMAT_R32G32_SFLOAT, offsetof(GpuInstance, SinRot));

    desc.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.CullMode = VK_CULL_MODE_NONE;
    desc.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.DepthTest = false;
    desc.DepthWrite = false;

    // Straight-alpha blending. Games that want premultiplied can submit a
    // separate feature or a variant pipeline later.
    ColorBlendAttachmentDesc blend{};
    blend.BlendEnable = true;
    blend.SrcColor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.DstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.ColorOp = VK_BLEND_OP_ADD;
    blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
    blend.DstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.AlphaOp = VK_BLEND_OP_ADD;
    desc.ColorBlend.push_back(blend);
    desc.ColorFormats.push_back(colorFormat);

    VkPipeline pipeline = Pipelines->GetGraphicsPipeline(desc);
    if (pipeline == VK_NULL_HANDLE)
    {
        return false;
    }
    CachedPipeline = pipeline;
    CachedColorFormat = colorFormat;
    return true;
}

void SpriteFeature::OnDraw(const FrameContext& frame)
{
    if (!Valid) return;
    if (Pending.empty()) return;

    if (!BuildPipeline(frame.TargetFormat))
    {
        Pending.clear();
        return;
    }

    // Stable sort by SortKey so same-key sprites preserve submission order.
    std::stable_sort(Pending.begin(), Pending.end(),
        [](const Sprite& a, const Sprite& b) { return a.SortKey < b.SortKey; });

    // Allocate a frame-UBO slot and write InvViewport. Even though the value
    // is the same for every sprite in one draw, we still flow it through the
    // dynamic UBO path so the plumbing stays exercised. Per-frame cost is
    // one 16-byte memcpy and one dynamic offset.
    const VulkanFrameScratch::Allocation uboAlloc =
        Scratch->AllocateUniform(sizeof(FrameUbo));
    if (!uboAlloc.IsValid())
    {
        Pending.clear();
        return;
    }

    FrameUbo ubo{};
    if (frame.TargetExtent.width > 0)
        ubo.InvViewport[0] = 1.0f / static_cast<float>(frame.TargetExtent.width);
    if (frame.TargetExtent.height > 0)
        ubo.InvViewport[1] = 1.0f / static_cast<float>(frame.TargetExtent.height);
    std::memcpy(uboAlloc.Mapped, &ubo, sizeof(ubo));

    // Allocate the instance vertex buffer through the same frame scratch.
    const VkDeviceSize instanceBytes =
        sizeof(GpuInstance) * static_cast<VkDeviceSize>(Pending.size());
    const VulkanFrameScratch::Allocation vboAlloc =
        Scratch->AllocateVertex(instanceBytes);
    if (!vboAlloc.IsValid())
    {
        Pending.clear();
        return;
    }

    auto* gpu = static_cast<GpuInstance*>(vboAlloc.Mapped);
    for (size_t i = 0; i < Pending.size(); ++i)
    {
        const Sprite& s = Pending[i];
        GpuInstance& g = gpu[i];
        g.Center[0] = s.CenterX;
        g.Center[1] = s.CenterY;
        g.HalfExtents[0] = s.Width * 0.5f;
        g.HalfExtents[1] = s.Height * 0.5f;
        g.UvMin[0] = s.UvMinX;
        g.UvMin[1] = s.UvMinY;
        g.UvMax[0] = s.UvMaxX;
        g.UvMax[1] = s.UvMaxY;
        g.Color = s.Color;
        g.TextureIndex = s.Texture.IsValid() ? s.Texture.Value : 0u;
        g.SinRot = std::sin(s.Rotation);
        g.CosRot = std::cos(s.Rotation);
    }

    // Both allocations share the same ring buffer; either handle resolves it.
    const VkBuffer ringBuffer = Buffers->GetBuffer(vboAlloc.Buffer);
    if (ringBuffer == VK_NULL_HANDLE)
    {
        Pending.clear();
        return;
    }

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, CachedPipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(frame.TargetExtent.width);
    viewport.height = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = frame.TargetExtent;
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

    VkDescriptorSet set = Descriptors->GetSet();
    const uint32_t dynamicOffset = static_cast<uint32_t>(uboAlloc.Offset);
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            PipelineLayout, 0, 1, &set, 1, &dynamicOffset);

    const VkDeviceSize vboOffset = vboAlloc.Offset;
    vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &ringBuffer, &vboOffset);

    vkCmdDraw(frame.Cmd, 6, static_cast<uint32_t>(Pending.size()), 0, 0);

    Pending.clear();
}
