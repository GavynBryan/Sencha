# Audio

The audio system is a three-layer stack: `AudioClip` holds raw PCM data,
`AudioBus` is a named pool of concurrent voices with shared volume controls,
and `AudioService` owns the SDL backend, allocates voices from buses, and
drives per-frame retirement of finished sounds.  Asset loading lives in
`AudioClipCache` alongside the other asset caches and is independent of the
playback service.

---

## Location

```
engine/include/audio/AudioService.h
engine/include/audio/AudioBus.h
engine/include/audio/AudioVoice.h
engine/include/audio/AudioConfig.h
engine/include/assets/audio/AudioClip.h
engine/include/assets/audio/AudioClipCache.h
engine/include/assets/audio/AudioClipLoader.h
```

```cpp
#include <audio/AudioService.h>
#include <assets/audio/AudioClipCache.h>
```

---

## System model

**Clips** are backend-agnostic CPU buffers.  `AudioClip` stores interleaved
`int16_t` samples, a sample rate, and a channel count.  The clip knows nothing
about streams, buses, or voices; it is a plain data struct.  Clips are loaded
from WAV files by `AudioClipCache` and resampled at play time to the device
spec (44100 Hz, stereo, Sint16).

**Buses** are named voice pools configured at startup.  Each bus has a maximum
voice count (1–255), a master volume, a mute flag, and a steal policy.  The
built-in `"Engine"` bus (MaxVoices=1, Reject) is always present and cannot be
configured or removed.  All other buses are declared in `EngineAudioConfig`
before `AudioService` is constructed.

**Voices** are individual playback streams.  Callers receive a `VoiceId` —
an opaque generational handle — and use it to stop, pause, or query a sound.
Stale handles from a recycled slot are silently rejected.

---

## API

```cpp
// -- Construction -----------------------------------------------------------

EngineAudioConfig audioConfig;
// (populate audioConfig.Buses — see Idiomatic setup below)
AudioService audio(logging, audioConfig);

if (!audio.IsValid())
    return; // SDL device failed to open

// -- Clip loading -----------------------------------------------------------

AudioClipCache clipCache(logging);

AudioClipHandle handle = clipCache.Acquire("sounds/explosion.wav");
// or RAII:
AudioClipCacheHandle handle = clipCache.AcquireOwned("sounds/explosion.wav");

const AudioClip* clip = clipCache.Get(handle); // nullptr if invalid/released
clipCache.Release(handle);                      // manual release

// -- Playback ---------------------------------------------------------------

PlayParams params;
params.Bus     = "Sfx";    // bus name (default: "Engine")
params.Gain    = 0.8f;     // [0, 1]   (default: 1.0)
params.Pan     = -0.5f;    // [-1, +1] (default: 0.0)
params.Looping = false;    //           (default: false)

VoiceId voice = audio.Play(AssetId{handle.Id}, *clip, params);
// Returns invalid VoiceId if: bus full + Reject policy, clip invalid, bus not found.
if (!voice.IsValid()) { /* bus was full */ }

audio.Stop(voice);   // immediate retire; no-op on invalid/stopped VoiceId
audio.Pause(voice);  // no-op if not Playing
audio.Resume(voice); // no-op if not Paused

// -- Bus controls -----------------------------------------------------------

audio.SetBusVolume("Sfx", 0.5f);   // returns false if bus name unknown
audio.SetBusMuted("Music", true);
float vol  = audio.GetBusVolume("Sfx");
bool muted = audio.IsBusMuted("Music");

// -- Voice state queries ----------------------------------------------------

bool        playing = audio.IsPlaying(voice);
bool        paused  = audio.IsPaused(voice);
VoiceState  state   = audio.GetState(voice);
// VoiceState: Idle | Playing | Paused | Stopped

// -- Per-frame update -------------------------------------------------------

audio.Tick(); // call once per frame; retires drained non-looping voices
```

`AudioService` is non-copyable and non-movable.

---

## Idiomatic setup

### SFX bus with voice stealing

Use `StealOldest` when only one instance of a sound should play at a time
and a new trigger should always succeed (e.g. a footstep or UI click).

```cpp
EngineAudioConfig audioConfig;

EngineAudioBusConfig sfx;
sfx.Name        = "Sfx";
sfx.MaxVoices   = 8;
sfx.Volume      = 1.0f;
sfx.StealPolicy = VoiceStealPolicy::StealOldest;
audioConfig.Buses.push_back(std::move(sfx));

AudioService audio(logging, audioConfig);
```

### Music bus with rejection

Use `Reject` for music so that a second `Play` call does not silently cut the
currently playing track.  Check the returned `VoiceId` before assuming
playback started.

```cpp
EngineAudioBusConfig music;
music.Name        = "Music";
music.MaxVoices   = 1;
music.Volume      = 0.7f;
music.StealPolicy = VoiceStealPolicy::Reject;
audioConfig.Buses.push_back(std::move(music));

// At runtime:
if (musicVoice.IsValid() && audio.IsPlaying(musicVoice))
    audio.Stop(musicVoice);

const AudioClip* track = clipCache.Get(trackHandle);
musicVoice = audio.Play(AssetId{trackHandle.Id}, *track,
                        PlayParams{ .Bus = "Music", .Looping = true });
```

### RAII clip lifetime

`AcquireOwned` wraps the handle in an `AudioClipCacheHandle` that releases
the clip automatically on destruction.  Use this when the clip should live
exactly as long as the owning object.

```cpp
struct Weapon
{
    AudioClipCacheHandle FireSound;
};

Weapon weapon;
weapon.FireSound = clipCache.AcquireOwned("sounds/gunshot.wav");

// In fire logic:
const AudioClip* clip = clipCache.Get(weapon.FireSound.Get());
if (clip)
    audio.Play(AssetId{weapon.FireSound.Get().Id}, *clip,
               PlayParams{ .Bus = "Sfx" });

// weapon destructor: FireSound calls clipCache.Release() automatically.
```

### Per-frame tick placement

`Tick()` retires voices whose SDL streams have drained.  Call it once per
frame, after processing all `Play` / `Stop` calls for that frame.

```cpp
// Main loop:
while (running)
{
    ProcessEvents();
    Update();
    Render();
    audio.Tick(); // last; retires finished voices
}
```

---

## JSON configuration

When `AudioService` is constructed from an engine config file, the `"audio"`
section is deserialized by `DeserializeAudioConfig`.  Do not list the built-in
`"Engine"` bus here.

```json
{
  "audio": {
    "buses": [
      {
        "name":        "Sfx",
        "maxVoices":   8,
        "volume":      1.0,
        "muted":       false,
        "stealPolicy": "StealOldest"
      },
      {
        "name":        "Music",
        "maxVoices":   1,
        "volume":      0.7,
        "muted":       false,
        "stealPolicy": "Reject"
      }
    ]
  }
}
```

Valid `stealPolicy` values: `"Reject"`, `"StealOldest"`.

---

## Constraints

**The built-in `"Engine"` bus must not appear in `EngineAudioConfig`.**  It is
hardcoded to MaxVoices=1 with Reject policy and is always constructed first.
Declaring a bus with the same name is undefined behaviour.

**Buses are fixed at construction.**  There is no API to add, remove, or
resize a bus after `AudioService` is constructed.  All buses must be declared
in `EngineAudioConfig` before calling the constructor.

**`MaxVoices` must be in the range [1, 255].**  Zero is invalid.  The field is
a `uint8_t`; values above 255 cannot be represented.

**The device spec is fixed at 44100 Hz, stereo, Sint16.**  Clips are resampled
to this spec via an SDL audio stream at play time.  There is no API to change
the output spec.

**`AudioClip` must remain alive for the duration of playback.**  `AudioService`
holds a const reference to the clip's samples for the life of the voice.  For
non-looping clips the samples are pushed into the SDL stream in `Play()`, so
the clip can be released once the call returns.  For looping clips the stream
is kept open and re-fed from the same buffer; the clip must not be freed until
after `Stop()` is called on the voice.

**`Tick()` must be called once per frame.**  Without it, non-looping voices
are never retired and their slots are not returned to the free pool.  Missing
`Tick()` calls will exhaust bus voice capacity over time.

**A zero or invalid `VoiceId` is always safe to pass to any control method.**
`Stop`, `Pause`, `Resume`, `IsPlaying`, `IsPaused`, and `GetState` silently
no-op on invalid handles.  There is no need to guard call sites with
`IsValid()` checks on the hot path.

**`AudioService` is non-copyable and non-movable.**  It owns the SDL audio
device handle for its entire lifetime.  Construct it once and pass it by
reference.

---

## Relationship to the engine

`AudioService` is a standalone service with no dependency on the ECS, render
pipeline, or game loop.  The full audio stack composes as:

```
AudioClip                 raw PCM data: samples + rate + channel count
      │
AudioClipCache            path-keyed ref-counted loader; returns AudioClipHandle
      │                   owns clip memory until refcount reaches zero
      │
AudioService              SDL backend, bus table, voice slots
      │                   Play() → VoiceId (opaque generational handle)
      │                   Tick() → retires drained non-looping voices
      │
Bus (AudioBus)            named voice pool: MaxVoices, Volume, Muted, StealPolicy
      │                   built-in "Engine" bus always present
      │
Voice (VoiceId)           per-play SDL_AudioStream bound to device
                          gain = params.Gain × bus.Volume (0 if muted)
```

`AudioClip` and `AudioClipCache` are independent of `AudioService`.  A clip
can be loaded, inspected, or serialized without an audio device.  A service
can be constructed against an empty config and clips loaded later.  Neither
layer reaches upward into the other.
