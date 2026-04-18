#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <world/World.h>

using World3d [[deprecated("Use Registry and explicit 3D transform services instead.")]] = World<Transform3f>;
