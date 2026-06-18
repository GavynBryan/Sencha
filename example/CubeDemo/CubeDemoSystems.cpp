#include "CubeDemoSystems.h"

#include <app/Engine.h>
#include <app/GameContexts.h>
#include <audio/Caption.h>
#include <audio/CaptionRuntime.h>
#include <math/Quat.h>
#include <world/transform/TransformComponents.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace
{
    struct FreeCameraMovementSystem
    {
        FreeCameraMovementSystem(Registry*& registry, FreeCamera& freeCamera)
            : RegistryInstance(registry)
            , FreeCam(freeCamera)
        {
        }

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;

            FreeCam.TickFixed(
                ctx.Input,
                RegistryInstance->Components,
                static_cast<float>(ctx.Time.DeltaSeconds));
        }

        Registry*& RegistryInstance;
        FreeCamera& FreeCam;
    };

    struct CubeSpinSystem
    {
        CubeSpinSystem(Registry*& registry, DemoScene& scene)
            : RegistryInstance(registry)
            , Scene(scene)
        {
        }

        void FixedLogic(FixedLogicContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;

            LocalTransform* cube =
                RegistryInstance->Components.TryGet<LocalTransform>(Scene.CenterCube);
            if (cube != nullptr)
            {
                cube->Value.Rotation *= Quatf::FromAxisAngle(
                    Vec3d::Up(),
                    static_cast<float>(ctx.Time.DeltaSeconds));
            }
        }

        Registry*& RegistryInstance;
        DemoScene& Scene;
    };

    struct FreeCameraLookSystem
    {
        FreeCameraLookSystem(Registry*& registry, FreeCamera& freeCamera)
            : RegistryInstance(registry)
            , FreeCam(freeCamera)
        {
        }

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            if (RegistryInstance == nullptr)
                return;

            FreeCam.UpdateLook(ctx.Input);
            FreeCam.ApplyRotation(RegistryInstance->Components);
        }

        Registry*& RegistryInstance;
        FreeCamera& FreeCam;
    };

    // Console presenter for the "World" channel. Demonstrates the idiomatic
    // shape: pull Visible(channel) each frame, edge-detect new captions with
    // Sequence, diff live ids for retirement. Real HUD presenters follow the
    // same pattern — one per surface, each reading its own channel.
    struct CaptionConsoleSystem
    {
        explicit CaptionConsoleSystem(CaptionRuntime* captions) : Captions(captions) {}

        void FrameUpdate(FrameUpdateContext&)
        {
            if (Captions == nullptr)
                return;

            std::span<const ActiveCaption> active = Captions->Visible("World");

            for (const ActiveCaption& caption : active)
            {
                if (caption.Sequence < NextUnseenSequence)
                    continue;
                NextUnseenSequence = caption.Sequence + 1;

                const std::string_view kind = CaptionKindToString(caption.Payload.Kind);
                std::printf("[caption] + %s | %.*s | %s%s\"%s\"",
                    caption.Payload.Channel.Data,
                    static_cast<int>(kind.size()), kind.data(),
                    caption.Payload.Speaker.Data,
                    caption.Payload.Speaker.Empty() ? "" : ": ",
                    caption.Payload.Text.Data);
                if (caption.Voice.IsValid())
                    std::printf(" (voice-bound)");
                if (caption.DurationSeconds > 0.0f)
                    std::printf(" (%.1fs cap)", caption.DurationSeconds);
                std::printf("\n");
            }

            // Anything tracked last frame and gone now has retired (voice
            // stopped, duration expired, or explicit end — all look the same
            // from outside, which is the point).
            for (const LiveEntry& entry : Live)
            {
                const bool stillActive = std::any_of(
                    active.begin(), active.end(),
                    [&](const ActiveCaption& c) { return c.Id == entry.Id; });
                if (!stillActive)
                    std::printf("[caption] - \"%s\" after %.1fs\n", entry.Text.Data, entry.AgeSeconds);
            }

            Live.clear();
            for (const ActiveCaption& caption : active)
                Live.push_back({ caption.Id, caption.Payload.Text, caption.AgeSeconds });
        }

        struct LiveEntry
        {
            CaptionId Id;
            CaptionTextKey Text;
            float AgeSeconds = 0.0f;
        };

        CaptionRuntime* Captions = nullptr;
        uint64_t NextUnseenSequence = 0;
        std::vector<LiveEntry> Live;
    };

    struct MouseTraceSystem
    {
        explicit MouseTraceSystem(FreeCamera& freeCamera)
            : FreeCam(freeCamera)
        {
        }

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            TraceHistory[TraceWrite] = TraceSample{
                .Dt = ctx.WallDeltaSeconds,
                .Mdx = ctx.Input.MouseDeltaX,
                .Mdy = ctx.Input.MouseDeltaY,
                .Yaw = FreeCam.Yaw,
                .Pitch = FreeCam.Pitch,
                .LookHeld = ctx.Input.IsMouseButtonDown(SDL_BUTTON_RIGHT),
            };
            TraceWrite = (TraceWrite + 1) % kTraceCapacity;
            if (TraceCount < kTraceCapacity)
                ++TraceCount;

            int numKeys = 0;
            const bool* sdlKeys = SDL_GetKeyboardState(&numKeys);
            const bool f2Down = sdlKeys != nullptr
                && SDL_SCANCODE_F2 < numKeys
                && sdlKeys[SDL_SCANCODE_F2];
            if (f2Down && !F2WasDown)
                DumpTrace();
            F2WasDown = f2Down;
        }

        void DumpTrace()
        {
            std::fprintf(stderr, "---- mouse trace (last %zu frames) ----\n", TraceCount);
            const size_t start = (TraceWrite + kTraceCapacity - TraceCount) % kTraceCapacity;
            for (size_t i = 0; i < TraceCount; ++i)
            {
                const TraceSample& s = TraceHistory[(start + i) % kTraceCapacity];
                std::fprintf(stderr, "[%02zu] dt=%.4f mdx=%+7.2f mdy=%+7.2f yaw=%+.4f pitch=%+.4f look=%d\n",
                    i, s.Dt, s.Mdx, s.Mdy, s.Yaw, s.Pitch, s.LookHeld ? 1 : 0);
            }
            std::fflush(stderr);
        }

        struct TraceSample
        {
            double Dt = 0.0;
            float Mdx = 0.0f;
            float Mdy = 0.0f;
            float Yaw = 0.0f;
            float Pitch = 0.0f;
            bool LookHeld = false;
        };

        static constexpr size_t kTraceCapacity = 120;
        std::array<TraceSample, kTraceCapacity> TraceHistory{};
        size_t TraceWrite = 0;
        size_t TraceCount = 0;
        bool F2WasDown = false;
        FreeCamera& FreeCam;
    };
}

void RegisterCubeDemoSystems(EngineSchedule& schedule,
                             Registry*& registry,
                             FreeCamera& freeCamera,
                             DemoScene& scene,
                             CaptionRuntime* captions)
{
    schedule.Register<CaptionConsoleSystem>(captions);
    schedule.Register<MouseTraceSystem>(freeCamera);
    schedule.Register<FreeCameraLookSystem>(registry, freeCamera);
    schedule.Register<FreeCameraMovementSystem>(registry, freeCamera);
    schedule.Register<CubeSpinSystem>(registry, scene);
}
