#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

//=============================================================================
// CaptionChannelConfig
//
// Load-time description of one caption presentation channel — the bus
// pattern: routing vocabulary is game config, not an engine enum
// (docs/audio/captions-and-dialogue.md, Decision A). Compiled into
// CaptionRuntime channel state at construction.
//
// GateOnSettings = false makes the channel visible regardless of the
// subtitle/CC accessibility toggles. That is the dialogue-menu shape: line
// text is ordinary UI even when passive subtitles are off.
//=============================================================================
struct CaptionChannelConfig
{
    std::string Name;
    bool GateOnSettings = true;
    uint8_t MaxVisibleLines = 3;   // 0 = unlimited
    bool MergeDuplicates = true;
};

//=============================================================================
// EngineCaptionConfig
//
// Top-level caption config passed to CaptionRuntime at construction time.
// An empty channel list means "use the engine defaults": World (gated, 3
// lines), Cutscene (gated, 2), UI (gated, 2) — opinionated toward the
// action/adventure posture, replaceable wholesale by any game.
//=============================================================================
struct EngineCaptionConfig
{
    std::vector<CaptionChannelConfig> Channels;
};

//=============================================================================
// CaptionConfigError
//=============================================================================
struct CaptionConfigError
{
    std::string Message;
};

// Deserialize EngineCaptionConfig from the "captions" section of the engine
// config JSON object (i.e. pass root.Find("captions") here, or the object
// itself if the captions section is the root).
std::optional<EngineCaptionConfig> DeserializeCaptionConfig(
    const JsonValue& root,
    CaptionConfigError* error = nullptr);
