#pragma once

#include "../editmodes/IManipulator.h"
#include "../level/LevelScene.h"
#include "../level/brush/BrushMesh.h"
#include "../meshedit/MeshElements.h"
#include "../selection/SelectionService.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <vector>

class ManipulatorSession;
class VulkanBufferService;
class VulkanFrameScratch;
class VulkanPipelineCache;
class VulkanShaderCache;

class SelectionRenderer
{
public:
    SelectionRenderer(LevelScene& scene, SelectionService& selection, ManipulatorSession& session);

    void Setup(const RendererServices& services);
    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport);
    void Teardown();

private:
    struct LineVertex
    {
        Vec3d Position;
        Vec4 Color;
    };

    struct LinePushConstants
    {
        Mat4 ViewProjection;
    };

    void AppendBrushMesh(std::vector<LineVertex>& vertices,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color) const;
    void AppendFace(std::vector<LineVertex>& vertices,
                    const FaceElement& face,
                    const Vec4& color) const;
    void AppendEdge(std::vector<LineVertex>& vertices,
                    const EdgeElement& edge,
                    const Vec4& color) const;
    void AppendVertex(std::vector<LineVertex>& vertices,
                      const VertexElement& vertex,
                      const Vec4& color) const;
    void AppendManipulators(std::vector<LineVertex>& vertices,
                            const EditorViewport& viewport) const;

    LevelScene& Scene;
    SelectionService& Selection;
    ManipulatorSession& Session;
    VulkanBufferService* Buffers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkFormat CachedColor = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepth = VK_FORMAT_UNDEFINED;
};
