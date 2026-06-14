#pragma once

#include <assets/cook/AssetImporter.h>

//=============================================================================
// Audio cook (docs/assets/pipeline.md, Decisions B, F). Dev-only — compiled
// under SENCHA_ENABLE_COOK, never shipped.
//
// WAV and OGG sources decode to Sint16 interleaved PCM at cook and emit a
// .sclip container; the runtime never decodes source formats for cooked
// content. OGG decode is stb_vorbis (cook-only dependency, the stb
// precedent); WAV decode shares LoadAudioClipFromWavBytes with the loose-
// bytes runtime fallback. No resampling: the clip keeps the source's rate
// and AudioService resamples on playback.
//=============================================================================

//=============================================================================
// AudioClipImporter — .wav/.ogg → cooked .sclip.
//
// The artifact keeps the source's virtual path ("asset://.../boop.wav"
// serves cooked .sclip bytes): authored references never churn when the
// cooked format evolves. The physical artifact lives at
// ".cooked/<source-path>.sclip".
//=============================================================================
class AudioClipImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
};
