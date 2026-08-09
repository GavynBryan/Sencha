#pragma once

#include <input/InputActionSource.h>
#include <world/ComponentRegistrar.h>

// Which input source steers an entity. Absent means this machine's own, so
// single-player content never carries one.
inline void RegisterInputComponents(ComponentRegistrar& registrar)
{
    registrar.Add<InputActionSourceRef>();
}
