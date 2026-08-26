#pragma once

#include <core/handle/Handle.h>

// Generational handle to a compiled shader module owned by the backend shader
// cache. One of the engine's unified Handle<Tag> types (handle convergence).
//
// Declared apart from the cache so types that merely *hold* a shader handle
// do not pull the Vulkan headers in behind it: the graphics/BufferHandle.h
// precedent. Code that loads or resolves shaders includes
// graphics/vulkan/VulkanShaderCache.h.
using ShaderHandle = Handle<struct ShaderHandleTag>;
