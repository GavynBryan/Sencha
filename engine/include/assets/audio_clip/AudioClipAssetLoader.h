#pragma once

#include <audio/AudioClipCache.h>
#include <core/assets/AssetLoader.h>
#include <core/logging/Logger.h>

class LoggingProvider;

//=============================================================================
// AudioClipAssetLoader
//
// Staged-load contract for audio clips (docs/assets/pipeline.md, Decisions
// C and F). Stage: bytes -> AudioClip — a validated copy for cooked .sclip,
// an SDL decode for loose WAV bytes (dev convenience, the loose-PNG
// precedent). Commit: register with AudioClipCache. Payload type:
// AudioClip.
//
// The whole round trip is CPU-side — the first loader with no GPU half,
// which is what makes it the cheap proof that adding a type is bounded
// (Decision F).
//=============================================================================
class AudioClipAssetLoader final : public IAssetLoader
{
public:
    AudioClipAssetLoader(LoggingProvider& logging, AudioClipCache* cache);

    [[nodiscard]] AssetType Type() const override { return AssetType::Audio; }
    [[nodiscard]] AssetStaging LoadStaged(const AssetRecord& record,
                                          IAssetSource& source) override;
    AssetCommitResult Commit(AssetStaging&& staged) override;

    // Owner-thread commit returning the typed handle (refcount 1, owned by
    // the caller). The virtual Commit wraps this for heterogeneous drivers.
    [[nodiscard]] AudioClipHandle CommitTyped(AssetStaging&& staged);

private:
    Logger& Log;
    AudioClipCache* Cache = nullptr;
};
