#pragma once

#include <registry/Registry2d.h>

// Deprecated compatibility shim. New code should use Registry2d.
class [[deprecated("Use Registry2d instead.")]] World2d : public Registry2d
{
public:
    explicit World2d(const PhysicsConfig2D& physicsConfig = {})
        : Registry2d(physicsConfig)
    {
    }
};
