#pragma once

#include <core/handle/Handle.h>

// Opaque handle to a resident font face in FontFaceCache. The alias lives here
// rather than in the cache header so a holder can name what it keeps without
// pulling in the cache that owns it.
using FontFaceHandle = Handle<struct FontFaceHandleTag>;
