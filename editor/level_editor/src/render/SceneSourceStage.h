#pragma once

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

class BrushBakeCache;
class EditorDocument;
class EditorScene;
class LoggingProvider;
class SceneRenderQueueBuilder;
struct BrushEvaluationPolicy;
struct RuntimeAssets;

// A scene source placed in a scratch document exactly as a level places it, so
// what is drawn from it is what the placement will be; never an open document.
// Queues exist only with an asset environment. docs/editor/placement.md
class SceneSourceStage
{
public:
    // `assetPath` placed at the identity; null with `error` set when it cannot
    // be placed.
    [[nodiscard]] static std::unique_ptr<SceneSourceStage> Prepare(const std::string& assetPath,
                                                                   std::vector<std::filesystem::path> roots,
                                                                   RuntimeAssets* assets, LoggingProvider& logging,
                                                                   std::string* error = nullptr);
    ~SceneSourceStage();

    SceneSourceStage(const SceneSourceStage&) = delete;
    SceneSourceStage& operator=(const SceneSourceStage&) = delete;

    // The placement's root transform, as editing a placement sets it.
    void Pose(const Transform3f& placement);
    // The preview piece budget its brushes evaluate under, as the views'.
    void SetEvaluationPolicy(const BrushEvaluationPolicy& policy);
    // Propagates transforms, then builds the queues when there are any.
    void Build();

    [[nodiscard]] const EditorDocument& Document() const { return *Doc; }
    [[nodiscard]] const EditorScene& Scene() const;
    // Null without an asset environment.
    [[nodiscard]] const SceneRenderQueueBuilder* Queues() const { return Builder.get(); }
    [[nodiscard]] EntityId Root() const { return RootEntity; }

    // Whether a source it placed would read differently from disk now.
    [[nodiscard]] bool ChangedOnDisk();
    // Whether it placed any of `assetPaths`, directly or nested.
    [[nodiscard]] bool DependsOnAny(std::span<const std::string> assetPaths) const;

private:
    SceneSourceStage() = default;

    // In dependency order, so they go in reverse: the builder holds the bakes.
    std::unique_ptr<EditorDocument> Doc;
    std::unique_ptr<BrushBakeCache> Bakes;
    std::unique_ptr<SceneRenderQueueBuilder> Builder;
    EntityId RootEntity;
};
