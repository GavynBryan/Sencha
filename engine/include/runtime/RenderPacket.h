#pragma once

#include <render/Camera.h>
#include <render/RenderQueue.h>
#include <time/FrameClock.h>

#include <cstdint>

//=============================================================================
// RenderPacket
//
// The sim → render handoff. Contains everything the renderer needs to draw
// a frame without touching simulation state. Simulation fills packet N and
// the renderer consumes it the same frame today; tomorrow the renderer can
// consume packet N-1 on a worker thread without changing the boundary.
//
// Lifetime: owned by the FrameDriver, double-buffered. Sim writes Active()
// during the ExtractRenderPacket phase; renderer reads ActiveForRender()
// during the Render phase. FrameDriver flips the pair at frame end.
//=============================================================================
struct RenderPacket
{
    CameraRenderData Camera{};
    RenderQueue Queue;

    PresentationTime Presentation{};
    FixedSimTime LastTick{};
    uint64_t FrameIndex = 0;

    bool HasCamera = false;
    bool Renderable = false;

    void Reset()
    {
        Queue.Reset();
        Camera = CameraRenderData{};
        Presentation = PresentationTime{};
        LastTick = FixedSimTime{};
        FrameIndex = 0;
        HasCamera = false;
        Renderable = false;
    }
};

class RenderPacketDoubleBuffer
{
public:
    RenderPacket& WriteSlot() { return Slots[WriteIndex]; }
    RenderPacket& ReadSlot() { return Slots[WriteIndex ^ 1]; }
    [[nodiscard]] const RenderPacket& ReadSlot() const { return Slots[WriteIndex ^ 1]; }

    void Flip() { WriteIndex ^= 1; }

private:
    RenderPacket Slots[2];
    int WriteIndex = 0;
};
