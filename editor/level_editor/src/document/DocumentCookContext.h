#pragma once

#include "DocumentCookPaths.h"

#include <filesystem>
#include <string_view>

class CookArtifactTransaction;
class DocumentArtifactCatalog;
class CookStepProgress;
class LoggingProvider;
struct DocumentCookResult;

// The running state one document cook threads through its stages: the output
// root and resolved paths, the artifact transaction and catalog every stage
// stages into, the progress/cancellation channel, the log sink, and the result
// each stage fills. Bundled so a stage takes this plus only its own inputs,
// instead of re-listing the shared cluster in every signature. Holds
// references into ExecuteDocumentCook's frame; it does not own them.
struct DocumentCookContext
{
    const std::filesystem::path& AssetsRoot;
    const DocumentCookPaths& Paths;
    CookArtifactTransaction& Transaction;
    DocumentArtifactCatalog& Catalog;
    CookStepProgress& Progress;
    LoggingProvider& Logging;
    DocumentCookResult& Result;

    [[nodiscard]] std::string_view Stem() const { return Paths.Stem; }
};
