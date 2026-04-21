#pragma once

#include <cstdint>
#include <span>

struct EditorViewport;

struct IViewportLayout
{
    virtual std::span<EditorViewport> GetViewports() = 0;
    virtual EditorViewport* GetActiveViewport() = 0;
    virtual void OnResize(uint32_t width, uint32_t height) = 0;
    virtual ~IViewportLayout() = default;
};
