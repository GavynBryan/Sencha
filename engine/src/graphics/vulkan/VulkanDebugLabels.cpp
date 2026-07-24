#include <graphics/vulkan/VulkanDebugLabels.h>

namespace
{
    PFN_vkCmdBeginDebugUtilsLabelEXT BeginFn = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT EndFn = nullptr;
    PFN_vkCmdInsertDebugUtilsLabelEXT InsertFn = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT NameFn = nullptr;
}

namespace VulkanDebugLabels
{
    void Load(VkInstance instance)
    {
        if (instance == VK_NULL_HANDLE)
            return;
        BeginFn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT"));
        EndFn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT"));
        InsertFn = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT"));
        NameFn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT"));
    }

    void BeginLabel(VkCommandBuffer cmd, const char* name)
    {
        if (BeginFn == nullptr)
            return;
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        BeginFn(cmd, &label);
    }

    void EndLabel(VkCommandBuffer cmd)
    {
        if (EndFn != nullptr)
            EndFn(cmd);
    }

    void InsertLabel(VkCommandBuffer cmd, const char* name)
    {
        if (InsertFn == nullptr)
            return;
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        InsertFn(cmd, &label);
    }

    void NameObject(VkDevice device, VkObjectType type, std::uint64_t handle,
                    const char* name)
    {
        if (NameFn == nullptr || device == VK_NULL_HANDLE || handle == 0)
            return;
        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;
        NameFn(device, &info);
    }
}
