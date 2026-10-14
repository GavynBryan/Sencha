#include "SceneSourceStage.h"

#include "BrushBakeCache.h"
#include "BrushBakeGpu.h"
#include "SceneRenderQueueBuilder.h"

#include "brush/BrushEvaluation.h"
#include "document/EditorDocument.h"
#include "document/EditorScene.h"

#include <assets/runtime/RuntimeAssets.h>
#include <world/identity/PersistentEntityIndex.h>

#include <algorithm>

std::unique_ptr<SceneSourceStage> SceneSourceStage::Prepare(const std::string& assetPath,
                                                            std::vector<std::filesystem::path> roots,
                                                            RuntimeAssets* assets, LoggingProvider& logging,
                                                            std::string* error)
{
    std::unique_ptr<SceneSourceStage> stage(new SceneSourceStage());
    stage->Doc = std::make_unique<EditorDocument>(logging);
    stage->Doc->SetContentRoots(std::move(roots));
    if (assets != nullptr)
        stage->Doc->SetAssetEnvironment(*assets);

    const SceneInstanceId placed = stage->Doc->PlaceSceneInstance(assetPath, Transform3f::Identity(), {}, error);
    if (!placed.IsValid())
        return nullptr;
    const auto* index = stage->Doc->GetRegistry().Components.TryGetResource<PersistentEntityIndex>();
    stage->RootEntity = index != nullptr ? index->TryResolve(PersistentEntityId{ placed.Value }) : EntityId{};
    if (!stage->RootEntity.IsValid())
    {
        if (error != nullptr)
            *error = "the placement of '" + assetPath + "' has no root";
        return nullptr;
    }

    if (assets != nullptr)
    {
        stage->Bakes = std::make_unique<BrushBakeCache>(MakeBrushBakeGpu(*assets));
        stage->Builder = std::make_unique<SceneRenderQueueBuilder>(
            assets->Assets, *stage->Bakes, *assets->StaticMeshes, assets->Materials, assets->MaterialSets, logging,
            nullptr, assets->SkinnedMeshes.get());
    }
    return stage;
}

SceneSourceStage::~SceneSourceStage() = default;

const EditorScene& SceneSourceStage::Scene() const
{
    return Doc->GetScene();
}

void SceneSourceStage::Pose(const Transform3f& placement)
{
    Doc->GetScene().SetTransform(RootEntity, placement);
}

void SceneSourceStage::SetEvaluationPolicy(const BrushEvaluationPolicy& policy)
{
    Doc->GetScene().SetInteractiveEvaluationPolicy(policy);
}

void SceneSourceStage::Build()
{
    Doc->GetScene().RefreshDerivedTransforms();
    if (Builder != nullptr)
        Builder->Build(*Doc);
}

bool SceneSourceStage::ChangedOnDisk()
{
    return Doc->SourcesChangedOnDisk();
}

bool SceneSourceStage::DependsOnAny(std::span<const std::string> assetPaths) const
{
    return std::ranges::any_of(assetPaths, [this](const std::string& path) { return Doc->DependsOnSource(path); });
}
