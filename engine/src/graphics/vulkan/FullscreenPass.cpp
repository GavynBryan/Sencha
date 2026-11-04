#include <graphics/vulkan/FullscreenPass.h>

#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <shaders/kFullscreenVertSpv.h>

bool FullscreenPass::Setup(const RendererServices& services, std::uint32_t pushConstantBytes)
{
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    Shaders = services.Shaders;
    Pipelines = services.Pipelines;
    Descriptors = services.Descriptors;
    PushConstantBytes = pushConstantBytes;
    if (Device == VK_NULL_HANDLE || Shaders == nullptr || Pipelines == nullptr || Descriptors == nullptr)
        return false;

    VertexShader = Shaders->CreateModuleFromSpirv(kFullscreenVertSpv, kFullscreenVertSpvWordCount, "Fullscreen vertex");

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = pushConstantBytes;

    const VkDescriptorSetLayout setLayout = Descriptors->GetBindlessSetLayout();
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &Layout) != VK_SUCCESS)
    {
        Layout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void FullscreenPass::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        VertexShader = {};
    }
    if (Device != VK_NULL_HANDLE && Layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device, Layout, nullptr);
        Layout = VK_NULL_HANDLE;
    }
    Descriptors = nullptr;
    Pipelines = nullptr;
    Shaders = nullptr;
    Device = VK_NULL_HANDLE;
}

VkPipeline FullscreenPass::BuildPipeline(ShaderHandle fragmentShader, const AttachmentFormatKey& formats,
                                         const ColorBlendAttachmentDesc& blend,
                                         std::span<const ShaderSpecializationConstant> specialization) const
{
    if (Layout == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    GraphicsPipelineDesc desc{};
    desc.VertexShader = VertexShader;
    desc.FragmentShader = fragmentShader;
    desc.FragmentSpecializationConstants.assign(specialization.begin(), specialization.end());
    desc.Layout = Layout;
    desc.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.CullMode = VK_CULL_MODE_NONE;
    desc.DepthTest = false;
    desc.DepthWrite = false;
    desc.ColorBlend = { blend };
    desc.ColorFormats = { formats.Color };
    desc.DepthFormat = formats.Depth;
    desc.StencilFormat = formats.Stencil;
    return Pipelines->GetGraphicsPipeline(desc);
}

void FullscreenPass::Draw(VkCommandBuffer cmd, VkExtent2D extent, VkPipeline pipeline, const void* push) const
{
    const VkDescriptorSet bindless = Descriptors->GetBindlessSet();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Layout, 0, 1, &bindless, 0, nullptr);

    const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                               0.0f, 1.0f };
    const VkRect2D scissor{ { 0, 0 }, extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(cmd, Layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, PushConstantBytes, push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
