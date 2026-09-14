#pragma once

#include "EditorImmediatePipeline.h"
#include "EditorInstancedLinePipeline.h" // EditorLineInstance
#include "EditorLinePipeline.h"          // EditorLineVertex

#include <span>

// TRIANGLE_LIST twin of EditorInstancedLinePipeline: one face fill in its
// mesh's frame drawn once per placement, alpha-blended and unculled like the
// immediate fill so a selected face reads from either side.
class EditorInstancedFillPipeline
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
