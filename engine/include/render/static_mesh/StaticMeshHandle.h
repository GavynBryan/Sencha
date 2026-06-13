#pragma once

#include <core/handle/Handle.h>

// Versioned handle to a static mesh owned by StaticMeshCache. Slot 0 is null.
// One of the engine's unified Handle<Tag> types (handle convergence). Handles
// are transient — scene data references meshes by asset path, never by handle,
// so this carries no reflection.
using StaticMeshHandle = Handle<struct StaticMeshHandleTag>;
