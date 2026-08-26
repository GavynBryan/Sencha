#pragma once

#include <core/handle/Handle.h>

// Generational handle to a GPU buffer owned by VulkanBufferService. One of the
// engine's unified Handle<Tag> types (handle convergence).
//
// Declared apart from the service so render-domain types that merely *hold* a
// buffer handle -- mesh residency, caches -- do not pull the Vulkan and VMA
// headers in behind it. Code that creates, uploads, or destroys buffers
// includes graphics/vulkan/VulkanBufferService.h and gets this through it.
using BufferHandle = Handle<struct BufferHandleTag>;
