#pragma once

#include <core/handle/Handle.h>

// Generational handle to an offscreen render target owned by RenderTargetStore.
// One of the engine's unified Handle<Tag> types.
//
// Declared apart from the store, the graphics/BufferHandle.h precedent, so a
// render-domain type that merely *names* a target -- a declared frame view, a
// composition node -- does not pull the Vulkan headers in behind it. Code that
// creates, resizes, or reads targets includes
// graphics/vulkan/RenderTargetStore.h and gets this through it.
using RenderTargetId = Handle<struct RenderTargetIdTag>;
