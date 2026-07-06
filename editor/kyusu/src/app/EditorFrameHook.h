#pragma once

#include <app/GameContexts.h>

#include <functional>
#include <utility>

// Minimal frame-update system that invokes a callback once per frame. Used by
// EditorApp to drain async file-dialog results and refresh the window title.
class EditorFrameHook
{
public:
    explicit EditorFrameHook(std::function<void()> fn)
        : Fn(std::move(fn))
    {
    }

    void FrameUpdate(FrameUpdateContext&)
    {
        if (Fn)
            Fn();
    }

private:
    std::function<void()> Fn;
};
