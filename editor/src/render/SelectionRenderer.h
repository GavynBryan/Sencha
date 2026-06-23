#pragma once

#include "EditorLinePipeline.h"

#include "../editmodes/IManipulator.h"
#include "../level/LevelScene.h"
#include "../brush/BrushMesh.h"
#include "../meshedit/MeshElements.h"
#include "../selection/SelectionService.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

#include <vector>

class ManipulatorSession;

// Draws selection highlights (object/face/edge/vertex) and the active
// manipulators via the shared editor line pipeline. Gathers lines only.
class SelectionRenderer
{
public:
    SelectionRenderer(LevelScene& scene, SelectionService& selection,
                      ManipulatorSession& session, EditorLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport);

private:
    void AppendAABB(std::vector<EditorLineVertex>& vertices,
                    const BrushMesh& mesh,
                    const Transform3f& transform,
                    const Vec4& color) const;
    void AppendFace(std::vector<EditorLineVertex>& vertices,
                    const FaceElement& face,
                    const Vec4& color) const;
    void AppendEdge(std::vector<EditorLineVertex>& vertices,
                    const EdgeElement& edge,
                    const Vec4& color) const;
    void AppendVertex(std::vector<EditorLineVertex>& vertices,
                      const VertexElement& vertex,
                      const Vec4& color,
                      float radius) const;
    void AppendManipulators(std::vector<EditorLineVertex>& vertices,
                            const EditorViewport& viewport) const;

    LevelScene& Scene;
    SelectionService& Selection;
    ManipulatorSession& Session;
    EditorLinePipeline& Lines;
};
