#pragma once

#include <audio/AudioVoice.h>
#include <audio/Caption.h>
#include <core/config/CaptionConfig.h>
#include <core/logging/LoggingProvider.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class AudioService;

//=============================================================================
// CaptionRuntime (docs/audio/captions-and-dialogue.md)
//
// Engine-owned active caption state: lifetime, accessibility filtering,
// duplicate merging, and per-channel visible lists. It never draws — game
// presenters consume Visible()/Active() snapshots.
//
// The two rules everything here serves:
//
//   - A caption never appears merely because a clip played; it appears
//     because a semantic request asked for presentable text.
//   - The caption layer is at least as reliable as the voice pool: subtitle
//     text is never lost to a full bus, a stolen voice, or a missing audio
//     device (Decision C — playback failure degrades Subtitle payloads to
//     timed captions and drops ClosedCaption payloads, because a CC for a
//     sound that never happened would lie).
//
// Filtering happens at read time, not emission time (Decision B): every
// authored caption is stored, and visibility is computed against the current
// settings and channel config. Toggling subtitles mid-line therefore takes
// effect immediately, and ungated channels (dialogue menus) read their text
// regardless of the accessibility toggles.
//
// Unlike AudioService there is no device dependency: the runtime is always
// valid and caption behavior is fully headless-testable. Main thread only.
//=============================================================================
class CaptionRuntime
{
public:
    CaptionRuntime(LoggingProvider& logging, const EngineCaptionConfig& config);

    CaptionRuntime(const CaptionRuntime&) = delete;
    CaptionRuntime& operator=(const CaptionRuntime&) = delete;
    CaptionRuntime(CaptionRuntime&&) = delete;
    CaptionRuntime& operator=(CaptionRuntime&&) = delete;

    void SetSettings(const CaptionSettings& settings);
    [[nodiscard]] const CaptionSettings& Settings() const { return ActiveSettings; }

    // -- Begin / end ----------------------------------------------------------

    // Begin a caption tied to `voice`. While the voice plays (or is paused)
    // the caption lives, capped by payload.DurationSeconds when > 0. An
    // invalid voice takes the Decision C degrade path: Subtitle payloads
    // become timed captions (authored duration, else `clipDurationHintSeconds`
    // when > 0, else a reading-speed estimate — all clamped by settings);
    // ClosedCaption payloads are dropped and an invalid id is returned.
    CaptionId BeginVoiceCaption(VoiceId voice,
                                const CaptionPayload& payload,
                                const CaptionSource& source = {},
                                float clipDurationHintSeconds = 0.0f);

    // Begin a caption with no voice: text-first narration, timed lines.
    // durationSeconds <= 0 derives a duration the same way the degrade path
    // does.
    CaptionId BeginTimedCaption(const CaptionPayload& payload,
                                float durationSeconds,
                                const CaptionSource& source = {});

    // Ending an invalid or already-ended caption is a no-op, mirroring
    // AudioService::Stop — double-end must stay safe under zone churn.
    void EndCaption(CaptionId id);
    [[nodiscard]] bool IsActive(CaptionId id) const;

    // -- Per-frame update -------------------------------------------------------

    // Age captions, retire voice-bound captions whose voice is no longer
    // playing or paused (queried through `audio`, keeping AudioService
    // caption-unaware), and expire timed captions. Null `audio` (headless)
    // leaves voice-bound captions to explicit EndCaption or their duration
    // cap.
    void Tick(const AudioService* audio, float dtSeconds);

    // -- Snapshots ----------------------------------------------------------------

    // The visible captions for one channel: settings gate applied (unless the
    // channel opts out), sorted by priority then recency (newest first,
    // deterministic via Sequence), trimmed to the channel's MaxVisibleLines.
    // Spans are invalidated by the next Begin/End/Tick/SetSettings call.
    [[nodiscard]] std::span<const ActiveCaption> Visible(std::string_view channel) const;

    // Everything currently active, unfiltered, in begin order — the debug
    // view ("what was authored" vs. Visible's "what policy admitted") and the
    // escape hatch for presenters that want their own policy.
    [[nodiscard]] std::span<const ActiveCaption> Active() const;

    [[nodiscard]] std::size_t ActiveCount() const;

    // The caption log category, for systems that drive caption components —
    // their content warnings (orphan components and the like) belong in the
    // same place as the runtime's own.
    [[nodiscard]] Logger& ContentLog() const { return Log; }

private:
    struct Channel
    {
        CaptionChannelName Name;
        bool GateOnSettings = true;
        uint8_t MaxVisibleLines = 3;  // 0 = unlimited
        bool MergeDuplicates = true;
    };

    struct Slot
    {
        bool InUse = false;
        uint32_t Generation = 1;
        ActiveCaption Data;
    };

    Logger& Log;
    CaptionSettings ActiveSettings;
    std::vector<Channel> Channels;
    std::vector<Slot> Slots;
    std::vector<uint32_t> FreeSlots;
    uint64_t NextSequence = 0;

    // Snapshots are rebuilt lazily on read; mutations only mark them stale.
    // VisibleLists is parallel to Channels (one list per channel).
    mutable bool SnapshotsDirty = true;
    mutable std::vector<ActiveCaption> AllActive;
    mutable std::vector<std::vector<ActiveCaption>> VisibleLists;

    // Warn-once state: content errors log once and stay quiet after, the
    // unknown-bus pattern — the warning is the to-do list. Mutable so a
    // read of an unknown channel (a presenter typo) can warn too.
    mutable std::unordered_set<std::string> WarnedChannels;
    bool WarnedEmptyText = false;

    CaptionId BeginInternal(VoiceId voice,
                            const CaptionPayload& payload,
                            float resolvedDurationSeconds,
                            const CaptionSource& source);
    [[nodiscard]] float DeriveDuration(const CaptionPayload& payload,
                                       float clipDurationHintSeconds) const;
    [[nodiscard]] Channel* FindChannel(std::string_view name);
    [[nodiscard]] const Channel* FindChannel(std::string_view name) const;
    void WarnUnknownChannelOnce(std::string_view name) const;
    void RebuildSnapshots() const;

    [[nodiscard]] Slot* Resolve(CaptionId id);
    [[nodiscard]] const Slot* Resolve(CaptionId id) const;
    void Retire(uint32_t slotIndex);

    [[nodiscard]] static CaptionId MakeCaptionId(uint32_t index, uint32_t generation);
    [[nodiscard]] static uint32_t CaptionIndex(CaptionId id);
};
