#pragma once

#include <core/handle/Handle.h>

// Generational handle to a GPU image owned by the backend image service. One
// of the engine's unified Handle<Tag> types (handle convergence).
//
// Declared apart from the service so types that merely *hold* an image handle
// -- probe residency, texture caches -- do not pull the Vulkan and VMA
// headers in behind it: the graphics/BufferHandle.h precedent. Code that
// creates, uploads, or destroys images goes through graphics/GpuImages.h or,
// in the backend, graphics/vulkan/VulkanImageService.h.
using ImageHandle = Handle<struct ImageHandleTag>;
