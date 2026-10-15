#pragma once

#include <memory>

class BrushBakeCache;
class EditorDocument;
class EditorScene;
class LoggingProvider;
class SceneRenderQueueBuilder;
struct BrushEvaluationPolicy;
struct RuntimeAssets;

// A scratch document drawn beside the level, never an open document: its own
// brush bakes and render queues, which exist only with an asset environment.
// The placement ghost and the draft preview are each one.
class DocumentStage
{
public:
    DocumentStage(std::unique_ptr<EditorDocument> document, RuntimeAssets* assets, LoggingProvider& logging);
    ~DocumentStage();
    DocumentStage(const DocumentStage&) = delete;
    DocumentStage& operator=(const DocumentStage&) = delete;

    // The preview piece budget its brushes evaluate under, as the views'.
    void SetEvaluationPolicy(const BrushEvaluationPolicy& policy);
    // Propagates transforms, then builds the queues when there are any.
    void Build();

    [[nodiscard]] EditorDocument& Document() { return *Doc; }
    [[nodiscard]] const EditorDocument& Document() const { return *Doc; }
    [[nodiscard]] const EditorScene& Scene() const;
    // Null without an asset environment.
    [[nodiscard]] const SceneRenderQueueBuilder* Queues() const { return Builder.get(); }

private:
    // In dependency order, so they go in reverse: the builder holds the bakes.
    std::unique_ptr<EditorDocument> Doc;
    std::unique_ptr<BrushBakeCache> Bakes;
    std::unique_ptr<SceneRenderQueueBuilder> Builder;
};
