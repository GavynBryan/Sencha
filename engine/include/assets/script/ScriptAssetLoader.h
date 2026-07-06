#pragma once

#include <assets/script/ScriptCache.h>
#include <core/assets/AssetLoader.h>
#include <core/logging/Logger.h>

class LoggingProvider;

//=============================================================================
// ScriptAssetLoader
//
// Staged-load contract for cooked T scripts (.tbc). CPU-only, modeled on
// AudioClipAssetLoader: LoadStaged reads the bytes, parses the container,
// and validates the bytecode (all pure, so it may run on a task thread);
// Commit registers the immutable module with ScriptCache. Payload type:
// std::shared_ptr<const ScriptModule>.
//
// The loader does not link against a World: linking (tag ids, ComponentId +
// offsets, host slots) is the world-side ScriptLink step, so one cooked
// module serves every world that loads it.
//=============================================================================
class ScriptAssetLoader final : public IAssetLoader
{
public:
    ScriptAssetLoader(LoggingProvider& logging, ScriptCache* cache);

    [[nodiscard]] AssetType Type() const override { return AssetType::Script; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    [[nodiscard]] ScriptHandle CommitTyped(AssetStaging&& staged);

    // Hot reload: same layout swaps in place, a layout change signals the
    // caller to reload the world. Returns the outcome so
    // the reloader can distinguish them; a failed staging is NotResident.
    [[nodiscard]] ScriptReloadResult CommitReload(AssetStaging&& staged);

private:
    Logger& Log;
    ScriptCache* Cache = nullptr;
};
