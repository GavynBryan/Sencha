#pragma once

#include <math/geometry/3d/Aabb3d.h>

// The portal's derived facing: the world axis (0/1/2) of minimum extent of its
// brush bounds. A thin box fitted into a wall faces through that wall; nothing
// is stored on the component. Ties resolve to the lowest axis index.
[[nodiscard]] int DominantPortalAxis(const Aabb3d& worldBounds);
