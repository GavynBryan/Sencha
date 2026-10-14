#pragma once

#include "DocumentStage.h"

#include <cstdint>
#include <memory>

class EditorDocument;
class LoggingProvider;
struct BrushEvaluationPolicy;
struct LevelDraft;
struct RuntimeAssets;

// What a draft would change, staged to be drawn over its level: the entities
// it adds or changes as they would be, and the ones it deletes as they are.
// Rebuilt when the draft moves on; never picked, never part of the level.
class DraftPreview
{
public:
    DraftPreview(RuntimeAssets* assets, LoggingProvider& logging);
    ~DraftPreview();

    // `base` is the level `draft` copies; a null draft shows nothing.
    void Update(const LevelDraft* draft, const EditorDocument* base, const BrushEvaluationPolicy& policy);

    // Null when the draft adds or changes nothing, or deletes nothing.
    [[nodiscard]] const DocumentStage* Added() const { return AddedStage.get(); }
    [[nodiscard]] const DocumentStage* Removed() const { return RemovedStage.get(); }

private:
    RuntimeAssets* Assets;
    LoggingProvider& Logging;
    std::uint64_t ShownInstance = 0;
    std::uint64_t ShownRevision = 0;
    std::unique_ptr<DocumentStage> AddedStage;
    std::unique_ptr<DocumentStage> RemovedStage;
};
