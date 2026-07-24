#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

// VK_EXT_debug_utils command labels and object names, in the VulkanBarriers
// free-function style. Load() resolves the entry points once per process;
// every call no-ops when the extension is absent or Load was never called.
// Command labels are per-frame commands and are emitted only while the
// profile mode is Gpu or above (the caller's gate); object names are
// creation-time metadata and cost nothing per frame.
//
// When render profiling is compiled out the bodies are excluded from the
// build and these inline stubs take over, so call sites carry no #ifdefs.
namespace VulkanDebugLabels
{
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    void Load(VkInstance instance);
    void BeginLabel(VkCommandBuffer cmd, const char* name);
    void EndLabel(VkCommandBuffer cmd);
    void InsertLabel(VkCommandBuffer cmd, const char* name);
    void NameObject(VkDevice device, VkObjectType type, std::uint64_t handle,
                    const char* name);
#else
    inline void Load(VkInstance) {}
    inline void BeginLabel(VkCommandBuffer, const char*) {}
    inline void EndLabel(VkCommandBuffer) {}
    inline void InsertLabel(VkCommandBuffer, const char*) {}
    inline void NameObject(VkDevice, VkObjectType, std::uint64_t, const char*) {}
#endif
}
