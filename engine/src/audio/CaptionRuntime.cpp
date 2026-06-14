#include <audio/CaptionRuntime.h>

#include <audio/AudioService.h>

#include <algorithm>
#include <cassert>
#include <utility>

// -- Enum string helpers (Caption.h) ------------------------------------------

std::string_view CaptionKindToString(CaptionKind kind)
{
    switch (kind)
    {
    case CaptionKind::Subtitle:      return "Subtitle";
    case CaptionKind::ClosedCaption: return "ClosedCaption";
    case CaptionKind::None:          break;
    }
    return "None";
}

std::optional<CaptionKind> CaptionKindFromString(std::string_view text)
{
    if (text == "None")          return CaptionKind::None;
    if (text == "Subtitle")      return CaptionKind::Subtitle;
    if (text == "ClosedCaption") return CaptionKind::ClosedCaption;
    return std::nullopt;
}

std::string_view CaptionPriorityToString(CaptionPriority priority)
{
    switch (priority)
    {
    case CaptionPriority::Ambient:   return "Ambient";
    case CaptionPriority::Narrative: return "Narrative";
    case CaptionPriority::Critical:  return "Critical";
    case CaptionPriority::Gameplay:  break;
    }
    return "Gameplay";
}

std::optional<CaptionPriority> CaptionPriorityFromString(std::string_view text)
{
    if (text == "Ambient")   return CaptionPriority::Ambient;
    if (text == "Gameplay")  return CaptionPriority::Gameplay;
    if (text == "Narrative") return CaptionPriority::Narrative;
    if (text == "Critical")  return CaptionPriority::Critical;
    return std::nullopt;
}

namespace
{
    // Reading-speed estimate for the last-resort derived duration: roughly
    // 15 characters per second, clamped by the settings bounds. The text is
    // a localization key by intent, so this is only ever a rough floor — an
    // authored duration or a clip duration hint always wins.
    constexpr float ReadingCharsPerSecond = 15.0f;

    // Whether the settings admit this kind on a gated channel.
    // ClosedCaptionsEnabled is the broader accessibility mode: it includes
    // speech, so subtitles show under either toggle.
    bool KindVisible(CaptionKind kind, const CaptionSettings& settings)
    {
        switch (kind)
        {
        case CaptionKind::Subtitle:
            return settings.SubtitlesEnabled || settings.ClosedCaptionsEnabled;
        case CaptionKind::ClosedCaption:
            return settings.ClosedCaptionsEnabled;
        case CaptionKind::None:
            break;
        }
        return false;
    }
}

CaptionRuntime::CaptionRuntime(LoggingProvider& logging, const EngineCaptionConfig& config)
    : Log(logging.GetLogger<CaptionRuntime>())
{
    auto addChannel = [this](const CaptionChannelConfig& cc)
    {
        if (cc.Name.empty())
        {
            Log.Error("CaptionRuntime: skipping channel config with empty name");
            return;
        }
        if (FindChannel(cc.Name) != nullptr)
        {
            Log.Error("CaptionRuntime: duplicate channel name '{}', skipping", cc.Name);
            return;
        }
        Channels.push_back(Channel{
            .Name = CaptionChannelName(cc.Name),
            .GateOnSettings = cc.GateOnSettings,
            .MaxVisibleLines = cc.MaxVisibleLines,
            .MergeDuplicates = cc.MergeDuplicates,
        });
    };

    if (config.Channels.empty())
    {
        // The engine default set: opinionated toward the action/adventure
        // posture, replaceable wholesale by game config (Decision A).
        addChannel({ .Name = "World", .MaxVisibleLines = 3 });
        addChannel({ .Name = "Cutscene", .MaxVisibleLines = 2 });
        addChannel({ .Name = "UI", .MaxVisibleLines = 2 });
    }
    else
    {
        for (const CaptionChannelConfig& cc : config.Channels)
            addChannel(cc);
    }

    VisibleLists.resize(Channels.size());
    Log.Info("CaptionRuntime: initialized ({} channels)", Channels.size());
}

void CaptionRuntime::SetSettings(const CaptionSettings& settings)
{
    ActiveSettings = settings;
    SnapshotsDirty = true;
}

CaptionId CaptionRuntime::BeginVoiceCaption(VoiceId voice,
                                            const CaptionPayload& payload,
                                            const CaptionSource& source,
                                            float clipDurationHintSeconds)
{
    if (voice.IsValid())
        return BeginInternal(voice, payload, payload.DurationSeconds, source);

    // Decision C degrade path: the caption layer must be at least as
    // reliable as the voice pool. Subtitles carry language the player must
    // not lose — they become timed. Closed captions describe a sound that
    // happened; no sound, no caption.
    if (payload.Kind != CaptionKind::Subtitle)
        return {};

    return BeginInternal({}, payload, DeriveDuration(payload, clipDurationHintSeconds), source);
}

CaptionId CaptionRuntime::BeginTimedCaption(const CaptionPayload& payload,
                                            float durationSeconds,
                                            const CaptionSource& source)
{
    const float duration = durationSeconds > 0.0f
        ? durationSeconds
        : DeriveDuration(payload, 0.0f);
    return BeginInternal({}, payload, duration, source);
}

CaptionId CaptionRuntime::BeginInternal(VoiceId voice,
                                        const CaptionPayload& payload,
                                        float resolvedDurationSeconds,
                                        const CaptionSource& source)
{
    // Kind::None is authored "no caption" — silent by design, not an error.
    if (payload.Kind == CaptionKind::None)
        return {};

    if (payload.Text.Empty())
    {
        if (!WarnedEmptyText)
        {
            WarnedEmptyText = true;
            Log.Warn("CaptionRuntime: caption with kind '{}' has empty text, suppressing",
                     CaptionKindToString(payload.Kind));
        }
        return {};
    }

    Channel* channel = FindChannel(payload.Channel.View());
    if (channel == nullptr)
    {
        WarnUnknownChannelOnce(payload.Channel.View());
        return {};
    }

    SnapshotsDirty = true;

    // Duplicate merge: a matching active caption is refreshed and rebound to
    // the new lifetime basis instead of adding a line — [gunfire] must not
    // stack 30 deep. Rebinding (voice + duration, not just age) matters for
    // loop re-entry: the old caption's voice is already dead, and a pure age
    // refresh would die with it on the next Tick.
    if (payload.MergeDuplicates && channel->MergeDuplicates)
    {
        for (Slot& slot : Slots)
        {
            if (!slot.InUse || !slot.Data.Payload.MergeDuplicates)
                continue;

            ActiveCaption& existing = slot.Data;
            if (existing.Payload.Channel == payload.Channel
                && existing.Payload.Text == payload.Text
                && existing.Payload.Speaker == payload.Speaker)
            {
                existing.Voice = voice;
                existing.DurationSeconds = resolvedDurationSeconds;
                existing.AgeSeconds = 0.0f;
                existing.Source = source;
                // A refresh is a renewal: bump the sequence so recency
                // ordering and trimming treat it as the newest line.
                existing.Sequence = ++NextSequence;
                return existing.Id;
            }
        }
    }

    uint32_t index;
    if (!FreeSlots.empty())
    {
        index = FreeSlots.back();
        FreeSlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(Slots.size());
        Slots.emplace_back();
    }

    Slot& slot = Slots[index];
    slot.InUse = true;
    slot.Data = ActiveCaption{
        .Id = MakeCaptionId(index, slot.Generation),
        .Voice = voice,
        .Payload = payload,
        .Source = source,
        .Sequence = ++NextSequence,
        .AgeSeconds = 0.0f,
        .DurationSeconds = resolvedDurationSeconds,
    };
    return slot.Data.Id;
}

void CaptionRuntime::EndCaption(CaptionId id)
{
    if (Resolve(id) != nullptr)
        Retire(CaptionIndex(id));
}

bool CaptionRuntime::IsActive(CaptionId id) const
{
    return Resolve(id) != nullptr;
}

void CaptionRuntime::Tick(const AudioService* audio, float dtSeconds)
{
    for (uint32_t index = 0; index < Slots.size(); ++index)
    {
        Slot& slot = Slots[index];
        if (!slot.InUse)
            continue;

        ActiveCaption& caption = slot.Data;
        caption.AgeSeconds += dtSeconds;

        // An authored/derived duration always caps, voice-bound or not.
        if (caption.DurationSeconds > 0.0f
            && caption.AgeSeconds >= caption.DurationSeconds)
        {
            Retire(index);
            continue;
        }

        if (caption.Voice.IsValid() && audio != nullptr)
        {
            // Stale ids (stolen, retired, swept voices) resolve to a
            // non-live state — the dormancy sweep needs no caption-aware
            // code, captions of stopped voices simply fall out here.
            const VoiceState state = audio->GetState(caption.Voice);
            if (state != VoiceState::Playing && state != VoiceState::Paused)
                Retire(index);
        }
    }

    SnapshotsDirty = true;
}

std::span<const ActiveCaption> CaptionRuntime::Visible(std::string_view channel) const
{
    const Channel* found = FindChannel(channel);
    if (found == nullptr)
    {
        WarnUnknownChannelOnce(channel);
        return {};
    }

    if (SnapshotsDirty)
        RebuildSnapshots();

    const std::size_t index = static_cast<std::size_t>(found - Channels.data());
    return VisibleLists[index];
}

std::span<const ActiveCaption> CaptionRuntime::Active() const
{
    if (SnapshotsDirty)
        RebuildSnapshots();
    return AllActive;
}

std::size_t CaptionRuntime::ActiveCount() const
{
    return Active().size();
}

float CaptionRuntime::DeriveDuration(const CaptionPayload& payload,
                                     float clipDurationHintSeconds) const
{
    float seconds = payload.DurationSeconds;
    if (seconds <= 0.0f)
        seconds = clipDurationHintSeconds;
    if (seconds <= 0.0f)
        seconds = static_cast<float>(payload.Text.Length()) / ReadingCharsPerSecond;

    return std::clamp(seconds,
                      ActiveSettings.DerivedDurationMinSeconds,
                      ActiveSettings.DerivedDurationMaxSeconds);
}

CaptionRuntime::Channel* CaptionRuntime::FindChannel(std::string_view name)
{
    for (Channel& channel : Channels)
        if (channel.Name.View() == name)
            return &channel;
    return nullptr;
}

const CaptionRuntime::Channel* CaptionRuntime::FindChannel(std::string_view name) const
{
    for (const Channel& channel : Channels)
        if (channel.Name.View() == name)
            return &channel;
    return nullptr;
}

void CaptionRuntime::WarnUnknownChannelOnce(std::string_view name) const
{
    if (WarnedChannels.insert(std::string(name)).second)
        Log.Warn("CaptionRuntime: unknown caption channel '{}', suppressing", name);
}

void CaptionRuntime::RebuildSnapshots() const
{
    AllActive.clear();
    for (const Slot& slot : Slots)
        if (slot.InUse)
            AllActive.push_back(slot.Data);

    // Begin order — deterministic, and chronological for presenters.
    std::sort(AllActive.begin(), AllActive.end(),
              [](const ActiveCaption& a, const ActiveCaption& b)
              { return a.Sequence < b.Sequence; });

    for (std::size_t channelIndex = 0; channelIndex < Channels.size(); ++channelIndex)
    {
        const Channel& channel = Channels[channelIndex];
        std::vector<ActiveCaption>& visible = VisibleLists[channelIndex];
        visible.clear();

        for (const ActiveCaption& caption : AllActive)
        {
            if (!(caption.Payload.Channel == channel.Name))
                continue;
            if (channel.GateOnSettings && !KindVisible(caption.Payload.Kind, ActiveSettings))
                continue;
            visible.push_back(caption);
        }

        // Priority outranks, then recency — newest first, deterministic via
        // the monotonic begin sequence.
        std::sort(visible.begin(), visible.end(),
                  [](const ActiveCaption& a, const ActiveCaption& b)
                  {
                      if (a.Payload.Priority != b.Payload.Priority)
                          return a.Payload.Priority > b.Payload.Priority;
                      return a.Sequence > b.Sequence;
                  });

        if (channel.MaxVisibleLines > 0 && visible.size() > channel.MaxVisibleLines)
            visible.resize(channel.MaxVisibleLines);
    }

    SnapshotsDirty = false;
}

CaptionRuntime::Slot* CaptionRuntime::Resolve(CaptionId id)
{
    return const_cast<Slot*>(std::as_const(*this).Resolve(id));
}

const CaptionRuntime::Slot* CaptionRuntime::Resolve(CaptionId id) const
{
    if (!id.IsValid())
        return nullptr;

    const uint32_t index = CaptionIndex(id);
    if (index >= Slots.size())
        return nullptr;

    const Slot& slot = Slots[index];
    if (!slot.InUse || slot.Data.Id != id)
        return nullptr;
    return &slot;
}

void CaptionRuntime::Retire(uint32_t slotIndex)
{
    assert(slotIndex < Slots.size());
    Slot& slot = Slots[slotIndex];
    slot.InUse = false;
    slot.Generation += 1;
    slot.Data = {};
    FreeSlots.push_back(slotIndex);
    SnapshotsDirty = true;
}

CaptionId CaptionRuntime::MakeCaptionId(uint32_t index, uint32_t generation)
{
    // Slots are 0-based but Handle reserves index 0 as the null slot, so the
    // stored Index is slot + 1 (a default-constructed CaptionId stays invalid).
    // Generation starts at 1 and a 32-bit counter never wraps in practice.
    return CaptionId{ index + 1, generation };
}

uint32_t CaptionRuntime::CaptionIndex(CaptionId id)
{
    return id.Index - 1;
}
