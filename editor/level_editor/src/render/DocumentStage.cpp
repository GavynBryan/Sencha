#include "DocumentStage.h"

#include "BrushBakeCache.h"
#include "BrushBakeGpu.h"
#include "SceneRenderQueueBuilder.h"

#include "brush/BrushEvaluation.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <assets/runtime/RuntimeAssets.h>

DocumentStage::DocumentStage(std::unique_ptr<EditorDocument> document, RuntimeAssets* assets,
                             LoggingProvider& logging)
    : Doc(std::move(document))
{
    if (assets == nullptr)
        return;
    Bakes = std::make_unique<BrushBakeCache>(MakeBrushBakeGpu(*assets));
    Builder = std::make_unique<SceneRenderQueueBuilder>(assets->Assets, *Bakes, *assets->StaticMeshes,
                                                        assets->Materials, assets->MaterialSets, logging, nullptr,
                                                        assets->SkinnedMeshes.get());
}

DocumentStage::~DocumentStage() = default;

const EditorScene& DocumentStage::Scene() const
{
    return Doc->GetScene();
}

void DocumentStage::SetEvaluationPolicy(const BrushEvaluationPolicy& policy)
{
    Doc->GetScene().SetInteractiveEvaluationPolicy(policy);
}

void DocumentStage::Build()
{
    Doc->GetScene().RefreshDerivedTransforms();
    if (Builder != nullptr)
        Builder->Build(*Doc);
}
