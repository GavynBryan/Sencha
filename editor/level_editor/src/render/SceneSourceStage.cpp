#include "SceneSourceStage.h"

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
    auto document = std::make_unique<EditorDocument>(logging);
    document->SetContentRoots(std::move(roots));
    if (assets != nullptr)
        document->SetAssetEnvironment(*assets);

    const SceneInstanceId placed = document->PlaceSceneInstance(assetPath, Transform3f::Identity(), {}, error);
    if (!placed.IsValid())
        return nullptr;
    const auto* index = document->GetRegistry().Components.TryGetResource<PersistentEntityIndex>();
    stage->RootEntity = index != nullptr ? index->TryResolve(PersistentEntityId{ placed.Value }) : EntityId{};
    if (!stage->RootEntity.IsValid())
    {
        if (error != nullptr)
            *error = "the placement of '" + assetPath + "' has no root";
        return nullptr;
    }
    stage->Stage = std::make_unique<DocumentStage>(std::move(document), assets, logging);
    return stage;
}

SceneSourceStage::~SceneSourceStage() = default;

void SceneSourceStage::Pose(const Transform3f& placement)
{
    Stage->Document().GetScene().SetTransform(RootEntity, placement);
}

bool SceneSourceStage::ChangedOnDisk()
{
    return Stage->Document().SourcesChangedOnDisk();
}

bool SceneSourceStage::DependsOnAny(std::span<const std::string> assetPaths) const
{
    return std::ranges::any_of(assetPaths, [this](const std::string& path) { return Stage->Document().DependsOnSource(path); });
}
