#include "CubeDemoSystems.h"

#include <app/GameContexts.h>
#include <math/Quat.h>

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>

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
                DemoTransforms(*RegistryInstance),
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

            if (Transform3f* cube = DemoTransforms(*RegistryInstance).TryGetLocalMutable(Scene.CenterCube))
            {
                cube->Rotation *= Quatf::FromAxisAngle(
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
            FreeCam.ApplyRotation(DemoTransforms(*RegistryInstance));
        }

        Registry*& RegistryInstance;
        FreeCamera& FreeCam;
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
                             DemoScene& scene)
{
    schedule.Register<MouseTraceSystem>(freeCamera);
    schedule.Register<FreeCameraLookSystem>(registry, freeCamera);
    schedule.Register<FreeCameraMovementSystem>(registry, freeCamera);
    schedule.Register<CubeSpinSystem>(registry, scene);
}
