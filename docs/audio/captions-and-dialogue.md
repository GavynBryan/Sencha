# Sencha Audio Captions And Dialogue Plan

Status: **implemented** (2026-06-12, branch `asset-pipelines`; see
"Implementation status" at the end). Extends the landed audio slice in
`docs/audio/runtime.md`. Decisions are tagged the usual way: **Settled** /
**Proposed** / **Open**.

The short version: captions, subtitles, and dialogue text attach to semantic
requests, not to raw clip playback. `AudioService` stays a small
voice/bus/streaming service. The engine adds a `CaptionRuntime` service that
owns caption lifetime and filtering, a small scene component that captions an
existing audio source, and **game-defined caption channels** that route text to
whatever presentation surfaces a game actually has.

## Revision 2 changes (what moved and why)

Revision 1 of this plan proposed an `AudioCue` asset type with its own cache,
loader, component, system, and service, plus an engine `CaptionContext` enum
and a `DialogueLine` runtime. Revision 2 reshapes four things:

1. **Caption channels are game data, not an engine enum.** Buses set the
   precedent: routing vocabulary is config-defined names resolved at runtime,
   because the engine cannot know a game's presentation surfaces. An enum
   (`Radio`, `DialogueMenu`, …) baked one genre's UI into engine headers and
   needed an engine recompile for a game that wants a "Holotape" channel or
   per-player channels in co-op.
2. **Filtering happens at read time, not emission time.** Revision 1 dropped
   caption events at emission when settings disabled them. That breaks the
   mid-line settings toggle (player enables subtitles during a long line —
   nothing appears), forced `DialogueMenu` special-casing, and made debugging
   harder. Now the runtime stores every authored caption; visibility is
   computed per channel against current settings each tick.
3. **Voice binding is timing, not existence.** Revision 1's "no voice → no
   caption" rule (Option A) silently lost narrative subtitles whenever a
   Reject bus was full — voice steal/reject is a *common* path, not an edge
   case. Now subtitles degrade to timed captions when playback fails; closed
   captions are dropped (no sound happened, so describing one would lie).
4. **Composition replaces the parallel cue stack.** A small
   `AudioCaptionComponent` beside the existing `AudioSourceComponent` replaces
   `AudioCue` + `AudioCueCache` + `AssetType::AudioCue` +
   `AudioCueSourceComponent` + `AudioCueService` for the first pass — five
   constructs become one component and one system, the "same clip meaningful
   here, decorative there" case falls out of which entity carries the caption
   component, and authors never face a "which of the two emitter components do
   I use?" question. Cue assets and `DialogueLine` move to deferred-with-
   triggers.

---

## Current Foundation

The engine currently has:

- `AudioService`: SDL-backed playback, buses with voice budgets and steal
  policies, `VoiceId` (generational, double-stop-safe), pause/resume,
  gain/pan/looping, per-frame `Tick()`. Note `Play` can fail and return an
  invalid `VoiceId` (Reject bus full, unknown bus, invalid clip).
- `AudioClipCache`: resident cooked clips loaded through `AssetSystem`.
- `AudioSourceComponent`: scene-authored emitter (`clip`, `bus`, `gain`,
  `pan`, `looping`, `play_on_active`; runtime `Voice`, `Started`), trivially
  copyable, with traits that retain on add and stop-then-release on remove.
- `AudioSystem`: runs in the audio lane (`FramePhase::Update`), ticks the
  service, sweeps voices for dormant registries, starts active scene sources.
- `AudioSourceRuntime`: the World resource carrying `{AudioClipCache*,
  AudioService*}` that component hooks reach through.
- Zone audio participation: dormant preloaded zones are silent by
  construction — absent from `FrameRegistryView::Audio`.

The landed invariant remains:

> A voice never outlives the clip reference that feeds it.

This plan adds a second invariant:

> A caption never appears merely because a clip played; it appears because a
> semantic request asked for presentable text.

And a third, new in revision 2:

> The caption layer is at least as reliable as the voice pool. Subtitle text
> must not be lost to a full bus, a stolen voice, or a missing audio device.

## Problem Statement

Different audio situations need different text behavior:

- Spoken dialogue over radio needs subtitles when subtitles are enabled.
- Meaningful non-speech sounds need closed captions when CC is enabled.
- Cutscene dialogue may need timeline-controlled subtitles.
- RPG dialogue-menu text is already presented by the dialogue UI and must not
  also spam the passive subtitle stack.
- Some raw SFX never need captions.
- The same clip can be background texture in one place and a gameplay clue in
  another.

Therefore captions cannot live on `AudioClip`, and they should not live inside
`AudioService`.

## Goals

- Keep `AudioService` focused on playback and voice state — this plan only
  *queries* it.
- Engine owns caption lifetime, filtering, merging, and routing state; game UI
  owns all presentation (layout, font, styling, animation).
- Separate subtitle settings from closed-caption settings.
- Presentation routing vocabulary is game-extensible without engine changes.
- Captioned audio is usable from both imperative gameplay code and
  scene-authored emitters.
- Scene data serializes through the existing `TypeSchema` / `SceneFieldCodec`
  patterns; the trivially-copyable component constraint is respected.
- Headless testability: no renderer, no real audio device for caption logic.

## Non-Goals

- No speech recognition or waveform-generated captions.
- No dialogue tree/graph system, no cutscene timeline, in this slice.
- No localization system implementation; this plan names the IDs and contracts
  it needs (Decision H).
- No UI renderer for text. The engine exposes channel snapshots only.
- No per-word karaoke timing or lip-sync.
- No music lyrics (separate licensing and presentation problem).

## Terminology

`CaptionKind`
: Accessibility classification — `Subtitle` (intelligible speech) vs
  `ClosedCaption` (meaningful non-speech sound). This is an engine enum
  because it is near-standardized accessibility semantics, not presentation.

Caption channel
: A named, game-defined presentation stream (`"World"`, `"Radio"`,
  `"Cutscene"`, `"DialogueMenu"`, …). Channels are config data, like buses.

`CaptionPayload`
: The authored request: kind, channel, priority, text key, speaker, duration.

`ActiveCaption`
: Runtime record held by `CaptionRuntime` while a caption lives.

`CaptionRuntime`
: Engine service owning active caption state: lifetime, settings, merging,
  per-channel visible lists. Never draws.

Presenter
: Game/UI-side consumer of channel snapshots. Not an engine construct.

## Decisions

### A. Kinds are engine enums; channels are game data

**Proposed** — this is the core reshape.

```cpp
enum class CaptionKind : uint8_t
{
    None,
    Subtitle,       // spoken dialogue or intelligible voice
    ClosedCaption,  // meaningful non-speech sound
};

enum class CaptionPriority : uint8_t
{
    Ambient,
    Gameplay,
    Narrative,
    Critical,
};

using CaptionChannelName = InlineString<32>;  // the BusName pattern
```

Kind and priority are engine enums: they encode accessibility filtering and
trim ordering, which the engine implements. The *channel* is a name resolved
against game config, exactly the way `PlayParams::Bus` resolves against the
bus table:

```cpp
struct CaptionChannelConfig
{
    CaptionChannelName Name;
    bool GateOnSettings = true;   // false: visible regardless of the
                                  // subtitle/CC toggles (dialogue menus,
                                  // accessibility-critical prompts)
    uint8_t MaxVisibleLines = 3;  // 0 = unlimited
    bool MergeDuplicates = true;  // channel-level default; payload can refuse
};

struct CaptionConfig
{
    std::vector<CaptionChannelConfig> Channels; // empty = engine defaults
};
```

`Configuration.Captions` sits beside `Configuration.Audio`. If a game defines
no channels, the engine provides a default set matching the action/adventure
posture: `World` (gated, 3 lines), `Cutscene` (gated, 2 lines), `UI` (gated,
2 lines). A game adds `Radio`, `DialogueMenu`, `P1`/`P2`, or replaces the set
wholesale. Opinionated defaults, zero lock-in.

Worked examples (channels here are *suggested config*, not engine names):

| Situation | Kind | Channel | Presentation |
| --------- | ---- | ------- | ------------ |
| Radio command | `Subtitle` | `Radio` | Passive lane with radio styling |
| NPC bark in world | `Subtitle` | `World` | Passive lane, speaker-tagged |
| Door slam that matters | `ClosedCaption` | `World` | `[door slams]` |
| Generator ambience | `None` (usually) | — | No caption unless authored |
| Cutscene spoken line | `Subtitle` | `Cutscene` | Cutscene surface |
| RPG dialogue choice | `Subtitle` | `DialogueMenu` | Dialogue UI (`GateOnSettings = false`), never the passive stack |
| Menu narrator | `Subtitle` | `UI` | UI surface |

Channel exclusivity (e.g., cutscenes suppressing world captions) is
deliberately **not** engine policy: a presenter that draws the cutscene
surface simply doesn't draw the passive surface while the cutscene channel is
non-empty. The engine keeps channels independent streams.

Unknown channel name at runtime: warn once per source, suppress — the
`alpha_mode: blend` / unknown-bus pattern. The warning is the to-do list.

### B. Filter at read time, not emission time

**Proposed.**

`CaptionRuntime` accepts and stores every authored caption whose kind is not
`None`. Settings and channel policy are applied when computing per-channel
visible lists during `Tick`. Consequences, all of them wanted:

- Toggling subtitles mid-line makes in-flight captions appear/disappear
  immediately — the standard accessibility expectation.
- A `GateOnSettings = false` channel (dialogue menu) gets its text regardless
  of the subtitle toggle, with no special-case code path.
- Debugging is "dump `Active()`, compare against `Visible(channel)`" — what
  was authored vs. what policy admitted, and why, is inspectable.
- Tests can assert on emission and visibility independently.

Visibility rules (defaults, applied to gated channels):

- `Subtitle` is visible when `SubtitlesEnabled || ClosedCaptionsEnabled`
  (closed captions are the broader mode and include speech).
- `ClosedCaption` is visible only when `ClosedCaptionsEnabled`.
- `Kind::None` is never stored.

```cpp
struct CaptionSettings
{
    bool SubtitlesEnabled = true;
    bool ClosedCaptionsEnabled = false;
    bool SpeakerNamesEnabled = true;     // presenter hint; engine never renders

    // Clamps for durations derived from voice/clip state.
    float DerivedDurationMinSeconds = 1.25f;
    float DerivedDurationMaxSeconds = 6.0f;
};
```

Per-surface presentation policy (line counts, merge behavior) lives in
`CaptionChannelConfig`, not in global settings — different surfaces genuinely
differ, and a game can change one channel without touching the others.

### C. Voice binding is timing, not existence

**Proposed.**

A caption's *existence* is decided by its payload and settings. A bound voice
only informs its *lifetime*. The rules:

- Voice valid: caption lives until the voice stops, is stolen, or is swept —
  unless the payload authored a finite `DurationSeconds`, which then caps it
  (`min(voice lifetime, duration)`). Looping sources should author a finite
  duration or accept a persistent caption — persistent is legal, e.g. a
  diegetic alarm.
- Playback failed (Reject bus full, missing clip, no/invalid audio device)
  and `Kind == Subtitle`: degrade to a **timed** caption. Duration is the
  authored `DurationSeconds` if set, else clip duration if known, else the
  derived-duration clamp. The player must not lose language content because
  the SFX bus was saturated during a firefight.
- Playback failed and `Kind == ClosedCaption`: **drop**. A closed caption
  describes a sound that happened; if no sound happened, `[door slams]` is a
  lie.

This dissolves revision 1's open Option A/B question (no-device behavior) into
the same rule: no device means voice creation fails, subtitles still arrive as
timed captions — which is exactly when a player needs text most — and CC
honestly stays silent because nothing sounded.

Tests stay honest the other way around: "bus reject still subtitles, with
derived duration" is pinned, instead of pinning the text loss.

### D. Scene authoring is composition: `AudioCaptionComponent`

**Proposed.**

A second component on the *same entity* as `AudioSourceComponent`, instead of
a parallel cue-emitter component referencing a new asset type:

```cpp
using CaptionTextKey = InlineString<64>;   // Decision H: a key, not display text
using SpeakerKey = InlineString<64>;

struct AudioCaptionComponent
{
    // -- Authored (serialized) ------------------------------------------------
    CaptionKind Kind = CaptionKind::ClosedCaption;
    CaptionChannelName Channel = "World";
    CaptionPriority Priority = CaptionPriority::Gameplay;
    CaptionTextKey Text;
    SpeakerKey Speaker;                 // empty = no speaker tag
    float DurationSeconds = 0.0f;       // 0 = derive from voice/clip
    bool MergeDuplicates = true;

    // -- Runtime (not serialized) ----------------------------------------------
    CaptionId Caption;        // generational, stale-safe like VoiceId
    VoiceId CaptionedVoice;   // which voice the current caption was begun for
    bool CaptionAttempted = false; // degrade-path fired once (Decision C)
};

static_assert(std::is_trivially_copyable_v<AudioCaptionComponent>, "...");
```

Why composition wins here:

- **Author experience.** Captioning an existing door slam is *adding one
  component* to the entity already in the scene — no new asset file on disk,
  no choosing between two emitter component types, no migrating the source to
  a cue. The revision-1 shape required authors to learn when
  `AudioSourceComponent` vs `AudioCueSourceComponent` applies; this shape has
  one emitter and an optional caption.
- **The context-dependence problem solves itself.** "Same clip, texture here,
  clue there" was the argument against captions-on-clip; cue assets only
  half-solved it (you'd author two cues or per-instance overrides). With
  composition the caption lives on the *placement*, which is exactly where
  the meaning lives.
- **Engine surface.** One component + one system (Decision E) instead of
  asset type + cache + loader + cook path + component + system + service.
  The runtime.md slice landed with "one component, one system, one service
  registration" — this keeps that budget.
- **Dormancy is nearly free** (see Decision F).

`AudioSourceComponent` stays untouched — raw, uncaptioned emitters keep
working and never pay for narrative fields. `AudioSourceRuntime` grows a
nullable `CaptionRuntime* Captions` pointer so the caption component's traits
can end captions on removal (headless worlds leave it null).

`TypeSchema` registration is the stamped template: one
`RegisterComponent<AudioCaptionComponent>()` line; `InlineString` fields
already have a codec from the bus-name work; the two enums need the small
enum-as-string codec treatment (author-readable scene JSON: `"kind":
"Subtitle"`, not `"kind": 1`).

### E. `CaptionSystem` — state-reactive, after `AudioSystem`

**Proposed.**

A small engine system in the same audio lane, ordered after `AudioSystem`:

1. Visit active-view entities with both `AudioSourceComponent` and
   `AudioCaptionComponent`:
   - Source's `Voice` valid and differs from `CaptionedVoice` → begin a
     voice-bound caption (covers first start *and* loop restart after zone
     re-entry, matching voice semantics: loops re-caption, one-shots don't
     replay because the voice never restarts).
   - Source `Started`, voice never became valid, `!CaptionAttempted` → the
     Decision C degrade path, once.
2. `CaptionRuntime::Tick(audio, dt)`: age captions, retire voice-bound
   captions whose voice died (queries `AudioService::GetState`, keeping
   `AudioService` caption-unaware), expire timed captions, merge duplicates,
   recompute per-channel visible lists (sorted by priority then recency,
   trimmed to `MaxVisibleLines`).

State-reactive matching is the Decision E precedent from runtime.md (gameplay
flips `PlayOnActive`, the system reacts next frame): no events, no coupling —
`AudioSystem` does not learn about captions, and `CaptionSystem` never starts
or stops voices. Same-frame ordering (source starts → caption begins) comes
from lane order, not communication.

`AudioService::Tick()` stays in `AudioSystem`. Revisit only if ordering gets
awkward (the open `AudioBackendSystem` question from revision 1, still
deferred).

### F. Dormancy and zone churn

**Proposed.** The landed rules carry over with almost no new machinery:

- Dormant registries are absent from the audio view, so `CaptionSystem` never
  begins captions for them.
- The existing `AudioSystem` sweep stops voices for registries that left the
  view; `CaptionRuntime::Tick` then retires their voice-bound captions
  *automatically* via voice state. The sweep table does not need to learn
  about caption ids — revision 1's `PlayingCue {Voice, Caption}` pair
  tracking is unnecessary.
- `AudioCaptionComponent`'s `OnRemove` ends its caption explicitly (entity
  destruction and zone detach), stale-safe like double-stop. Order relative
  to the source component's stop does not matter: ending an already-retired
  caption and retiring-by-dead-voice are both no-op-safe.
- One-shot semantics match sources: fire once per component lifetime, no
  caption replay on zone re-entry. Per-activation replay stays deferred with
  the same trigger as the source-side rule.

`CaptionId` must be generational or otherwise stale-safe. Timed captions from
the degrade path are bounded by duration, so a zone detaching mid-degraded-
caption merely lets it expire; `OnRemove`'s explicit end also covers it.

### G. Imperative surface — two calls now, cue assets when content asks

**Proposed.**

Gameplay code that plays a captioned sound writes:

```cpp
VoiceId voice = audio->Play(clipId, clip, params);
captions->BeginVoiceCaption(voice, payload, source);  // Decision C rules apply,
                                                      // including degrade on invalid voice
```

Text without sound (timed narration, text-only fallback):

```cpp
captions->BeginTimedCaption(payload, seconds, source);
```

That is the whole imperative API for this slice. `AudioService::Play` alone
remains the escape hatch for raw, uncaptioned audio — raw plays never emit
captions (the second invariant).

The revision-1 `AudioCue` asset (clip + default params + caption metadata as
a shareable asset, with cache, `AssetType`, JSON authoring, cook validation)
is **deferred with a trigger**: the same clip+caption payload duplicated
across roughly three or more call sites or scenes, or content wanting
data-driven cue tables. The concept is sound and slots in cleanly later — a
cue asset then becomes *one more way to construct a payload + play call*, and
nothing in this slice has to change shape to accept it. Designing the asset
now, before any consumer exists, would freeze metadata guesses into a cooked
format. If/when it lands, name it `AudioCue` (`SoundCue` invites confusion
with no matching `Sound*` family in the codebase).

### H. Localization posture

**Proposed**, unchanged in substance from revision 1.

- `CaptionTextKey` is an ID by intent (`"radio.bridge.open"`). The engine
  never interprets it; presenters resolve it. Tests and the debug presenter
  use a temporary literal map.
- A small game that wants no localization may put short literal text in the
  key and present it raw — the engine does not forbid it; the 64-char cap is
  the natural pressure toward real keys for shipping content.
- The ECS trivially-copyable constraint applies only to the *component*.
  `ActiveCaption` lives in a service and may later carry resolved
  `std::string` display text once a localization table exists — do not
  contort runtime storage to `InlineString` out of habit.
- Long-term (separate plan): localization table asset, cook validation that
  every referenced key exists per locale, missing-text warn-once + suppress
  in runtime builds.

## Runtime Data Shape

```cpp
struct CaptionPayload
{
    CaptionKind Kind = CaptionKind::None;
    CaptionChannelName Channel = "World";
    CaptionPriority Priority = CaptionPriority::Gameplay;

    CaptionTextKey Text;
    SpeakerKey Speaker;

    float DurationSeconds = 0.0f;  // 0 = derive (voice, clip, or clamp)
    bool MergeDuplicates = true;
};

struct CaptionSource
{
    EntityId Entity;
    // Later: ZoneId, world position, portrait id, faction, radio channel.
};

struct ActiveCaption
{
    CaptionId Id;
    VoiceId Voice;            // invalid for timed captions
    CaptionPayload Payload;
    CaptionSource Source;
    float AgeSeconds = 0.0f;
    float DurationSeconds = 0.0f;  // resolved (authored, derived, or 0 = voice-bound)
};
```

## `CaptionRuntime`

```cpp
class CaptionRuntime : public IService
{
public:
    CaptionRuntime(LoggingProvider& logging, const CaptionConfig& config);

    void SetSettings(const CaptionSettings& settings);
    [[nodiscard]] const CaptionSettings& Settings() const;

    // Decision C: an invalid voice degrades Subtitle payloads to timed and
    // drops ClosedCaption payloads. Kind::None is never stored.
    CaptionId BeginVoiceCaption(VoiceId voice,
                                const CaptionPayload& payload,
                                const CaptionSource& source = {});

    CaptionId BeginTimedCaption(const CaptionPayload& payload,
                                float durationSeconds,
                                const CaptionSource& source = {});

    void EndCaption(CaptionId id);              // stale-safe no-op
    [[nodiscard]] bool IsActive(CaptionId id) const;

    // Age, retire by voice state / duration, merge, recompute visible lists.
    // `audio` nullable (headless): voice-bound captions then retire by
    // explicit EndCaption or duration only.
    void Tick(const AudioService* audio, float dtSeconds);

    // Read-time filtering (Decision B). Spans are stable until the next Tick.
    [[nodiscard]] std::span<const ActiveCaption> Visible(std::string_view channel) const;
    [[nodiscard]] std::span<const ActiveCaption> Active() const;  // unfiltered, debug/UI-override
};
```

Constructed in `Engine::Initialize` from `Configuration.Captions` into
`ServiceHost`, beside `AudioService`. Unlike `AudioService` it has no device
dependency, so it is always valid — caption logic is fully headless-testable.

Revision 1 left "is `CaptionRuntime` a service immediately?" open and floated
starting `AudioCueService` game-side because `RuntimeAssets` is game-owned.
With the cue stack deferred, that boundary tension is gone: `CaptionRuntime`
has no asset dependencies at all and belongs in `ServiceHost` from day one.

## Priority, Merging, And Spam Control

Passive surfaces need guardrails or action games become unreadable. Defaults
(all per-channel config or payload data, not hardcoded):

- `MaxVisibleLines` default 3; Critical > Narrative > Gameplay > Ambient,
  newer outranks older within a priority.
- Merge key `{Text, Speaker, Channel}`: a matching active caption with
  `MergeDuplicates` true refreshes duration instead of adding a line —
  `[gunfire]` must not add 30 lines.
- World ambience should author `Kind::None` (i.e., no caption component)
  unless meaningful.
- Looping sources should author finite `DurationSeconds` unless a persistent
  caption is intended.

## Error Handling

Content errors warn once and stay non-fatal, matching the unknown-bus pattern:

- Unknown caption channel: warn once per source, suppress.
- Empty `Text` with `Kind != None`: warn once, suppress.
- Missing text key at presentation time: presenter concern now; cook
  validation once localization lands.
- Settings disable a caption: no warning — it is stored, just not visible.
- Caption component without an `AudioSourceComponent` sibling: warn once,
  inert. (A future text-only timed-caption component is possible but has no
  customer; imperative `BeginTimedCaption` covers it.)

## Rejected Alternatives

### Captions on `AudioClip`

Clips are raw reusable media; the same clip is meaningful in one placement
and decorative in another. (Unchanged from revision 1.)

### Captions in `AudioService`

The service should not know localization, speakers, channels, or
accessibility settings. This plan only ever *queries* it. (Unchanged.)

### Subtitle fields on `AudioSourceComponent`

Would blend playback, narrative, accessibility, and routing into one ECS type
and make every raw emitter carry dead fields. Composition (Decision D) gets
the ergonomics without the blending. (Unchanged conclusion, better remedy.)

### One global subtitle stream

Different surfaces have different routing, gating, and duplication rules.
(Unchanged — generalized into channels.)

### Engine-enum caption contexts (revision 1)

Baked one genre's presentation surfaces into engine headers; any new surface
(holotape, phone, per-player splits) meant an engine change. Channels-as-
config follows the bus precedent and removes the limit. The old
`DialogueMenu`-is-special filtering rule dissolves into `GateOnSettings`.

### Emission-time filtering (revision 1)

Lost in-flight captions on settings toggles, required context special-cases,
and hid "what was authored" from debugging. Read-time filtering stores all,
computes visibility.

### Cue-asset-first authoring (revision 1)

`AudioCue` + cache + asset type + second emitter component + cue system +
cue service before any content needed shared cues. Deferred with a concrete
trigger (Decision G); the payload/channel substrate is designed so cue assets
slot in additively later.

## Rollout Plan

### Stage 1: `CaptionRuntime`

`CaptionKind`, `CaptionPriority`, `CaptionChannelName`, `CaptionPayload`,
`CaptionId`, `CaptionSettings`, `CaptionConfig` + channel defaults,
`CaptionRuntime` in `ServiceHost`. No audio coupling beyond `VoiceId` as an
opaque value; no assets; fully headless.

Tests: subtitle/CC visibility under each settings combination; **mid-life
settings toggle flips visibility without re-emission**; `GateOnSettings =
false` channel visible with all settings off; priority trim determinism;
duplicate merge refresh; timed expiry; stale-safe `CaptionId` double-end;
unknown-channel warn-once.

### Stage 2: Voice binding

`Tick` retires voice-bound captions via `AudioService::GetState` (real
service on SDL's dummy driver, skip-if-unavailable — the established
precedent). Decision C rules.

Tests: voice stop retires caption; voice steal retires caption; **bus-Reject
play still produces a timed subtitle with derived duration**; bus-Reject
closed caption is dropped; authored duration caps a live voice's caption;
null/invalid audio service degrades subtitles and drops CC identically; raw
`AudioService::Play` emits nothing.

### Stage 3: Scene composition

`AudioCaptionComponent` + traits + `TypeSchema` (enum-as-string codec),
`CaptionRuntime*` added to `AudioSourceRuntime`, `CaptionSystem` registered
after `AudioSystem` in the audio lane. CubeDemo: caption the existing one-shot
(`ClosedCaption`/`World`) and add a captioned line to prove the subtitle path.

Tests: active zone starts source and caption together; dormant sweep stops
voice and caption retires without caption-aware sweep code; loop re-entry
re-captions, one-shot does not replay either; component removal ends caption
(both orders of component removal safe); scene JSON round-trips the component
with readable enum strings; caption component without source sibling is
inert + warned.

### Stage 4: Presenter proof

A debug/test presenter (log or test harness, no production UI) consuming
`Visible(channel)`.

**Gate:** a headless sample with one captioned world SFX (`ClosedCaption` →
`World`), one radio subtitle (`Subtitle` → game-defined `Radio` channel —
proving channels-as-config end to end), and one `GateOnSettings = false`
dialogue-menu-shaped line, each landing only in its own channel; toggling
`SubtitlesEnabled`/`ClosedCaptionsEnabled` mid-test flips exactly the gated
ones; visible-list ordering is deterministic across runs.

## Deferred, with triggers

- **`AudioCue` assets** (cache, `AssetType`, JSON authoring, cook
  validation): same clip+caption payload duplicated across ~3+ call
  sites/scenes, or content wanting data-driven cue tables.
- **`DialogueLine` / `DialogueRuntime`**: first narrative content (cutscene
  or conversation). The contract it builds on is already here — lines are
  payloads into game-defined channels, voice via `BeginVoiceCaption`, menus
  via an ungated channel. Until then a dialogue system is game code.
- **Localization table**: the localization plan. This plan only reserves
  `CaptionTextKey` semantics.
- **Cutscene timeline integration**: the timeline plan; timed captions are
  the substrate.
- **Text-only scene caption component** (no audio sibling): content request.
- **Per-activation one-shot replay**: same trigger as the source-side rule.
- **`AudioBackendSystem` split** (who ticks `AudioService`): ordering pain
  among multiple audio-lane systems.

## Future Work

- Spatialized caption hints: offscreen arrows, source direction, distance.
- Speaker styling/portraits (presenter-side; `CaptionSource` grows fields).
- Per-segment timing for long lines.
- Streamed VO and music (pipeline Decision F's later slice).
- Audio ducking while dialogue/radio captions are active.
- Accessibility presentation options (font scale, background opacity, speaker
  colors, safe areas) — presenter-side, listed so they aren't forgotten.

## First Implementation Recommendation

Do not touch `AudioService` except to query voice state.

Build `CaptionRuntime` first and pin the filtering/lifetime/degrade behavior
headless — it has no asset or device dependencies, so Stage 1 and 2 tests are
cheap and fast. Then add the component + system pair. Keep
`AudioSourceComponent` exactly as it is.

That sequence preserves the landed audio slice, keeps the new-surface budget
at one component + one system + one service (the same budget the audio slice
shipped with), and gives Sencha a path from captioned action/adventure SFX to
radio dialogue, cutscenes, and RPG dialogue menus — with the genre-specific
parts living in game config and presenters, where they belong.

## Implementation status (2026-06-12)

All four stages landed, test-verified (803 tests green, 24 new across
`test/runtime/CaptionRuntimeTests.cpp` and
`test/runtime/CaptionSystemTests.cpp`, full suite TSan-clean). The Stage 4
gate is pinned headless: world CC, a radio subtitle on a game-defined
channel, and an ungated dialogue-menu line route to three channels, settings
toggles flip exactly the gated ones, and ordering is deterministic.

What landed where:

- `audio/Caption.h` — the vocabulary (Decision A): kinds, priorities,
  `CaptionId` (generational, stale-safe), payload/source/settings,
  `ActiveCaption` with a monotonic `Sequence` for deterministic ordering.
- `core/config/CaptionConfig.h/.cpp` — `EngineCaptionConfig` (named with the
  `Engine*Config` house prefix) + channel deserializer, wired into
  `EngineConfig` as the `captions` section.
- `audio/CaptionRuntime.{h,cpp}` — the service, in `ServiceHost` from
  `Engine::Initialize`. Read-time filtering (Decision B) via lazily rebuilt
  snapshots (a dirty flag; spans invalidate on Begin/End/Tick/SetSettings).
  Merge is refresh-and-*rebind*: a merged duplicate adopts the new voice and
  duration, which is what keeps a loop-restart caption alive across the old
  voice's death. Default channels (World/Cutscene/UI) install when config is
  empty.
- `audio/AudioCaptionComponent.h` — the component (Decision D), traits
  (OnRemove ends the caption), TypeSchema; `SceneChunk::AudioCaptions`
  ('ACAP'); enum-as-string codecs in `SceneFieldCodec` (unknown strings fail
  the load). `AudioSourceRuntime` grew the nullable `Captions` pointer.
- `audio/CaptionSystem.{h,cpp}` — registered after `AudioSystem` (Decision
  E). One refinement discovered in implementation: with **no usable audio
  service at all**, `AudioSystem` no-ops, so `Started` never latches — the
  system therefore treats an active `PlayOnActive` source under a null
  service as failed playback, and its subtitle degrades to timed. Without
  this, the no-device player (invariant 3's whole point) would get nothing.
- `InlineString` gained `operator=(const char*)` — same ambiguity the
  converting constructor already solved, surfaced by assignment from string
  literals.
- CubeDemo: the one-shot cube chime carries an `AudioCaption`
  (`ClosedCaption`/`World`); the zone builder threads `CaptionRuntime*`
  through `AudioSourceRuntime`. The manifest needed no change —
  `AudioCaption` references no assets. The in-app graphical run remains owed
  to a machine with a display, the standing posture from the audio slice.
