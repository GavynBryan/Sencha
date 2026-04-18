#pragma once

#include <registry/Registry.h>
#include <span>

struct FrameRegistryView
{
    Registry* Global = nullptr;

    std::span<Registry*> Visible;
    std::span<Registry*> Physics;
    std::span<Registry*> Logic;
    std::span<Registry*> Audio;
};
