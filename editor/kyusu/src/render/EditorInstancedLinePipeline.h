#pragma once

#include "EditorImmediatePipeline.h"
#include "EditorLinePipeline.h" // EditorLineVertex

#include <math/Mat.h>

#include <span>

// One placement of an instanced line list: the model matrix by rows, exactly
// as the shader's four per-instance attributes read it.
struct EditorLineInstance
{
    Mat4 Model = Mat4::Identity();
};

// LINE_LIST configuration of the shared immediate pipeline with a per-instance
// model matrix: one mesh's edges in its own frame drawn once per placement.
// What the brush wireframe uses so a repeated brush uploads its edges once.
class EditorInstancedLinePipeline
{
public:
    void Setup(const RendererServices& services);
    void Submit(const FrameContext& frame,
                const EditorViewport& viewport,
                const CameraRenderData& camera,
                std::span<const EditorLineVertex> vertices,
                std::span<const EditorLineInstance> instances,
                bool onTop = false);
    void Teardown();

private:
    EditorImmediatePipeline<EditorLineVertex> Pipeline;
};
