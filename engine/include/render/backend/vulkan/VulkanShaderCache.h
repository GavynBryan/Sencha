#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class VulkanDeviceService;

//=============================================================================
// VulkanShaderCache
//
// Owns every VkShaderModule the engine creates. Shaders are referenced by
// opaque ShaderHandle values with generational validation, same pattern as
// VulkanBufferService / VulkanImageService.
//
// Two ingestion paths, both funneling through a single glslang entry point:
//
//   LoadFromFile(path, stage)
//       Reads a .glsl/.vert/.frag/.comp source file. Uses an on-disk SPIR-V
//       cache sitting next to the source (`foo.frag` -> `foo.frag.spv`).
//       Cache is mtime-validated: if the .spv is newer than the source we
//       reuse it, otherwise we recompile and rewrite it. This is the path
//       game code and built-in engine shaders should use.
//
//   CompileFromSource(source, stage, debugName)
//       Compiles an in-memory GLSL string. No disk cache touched. This is
//       the extension hook for generators that produce GLSL at runtime --
//       tests, fixtures, and (eventually) a node-editor frontend. The cache
//       does not care where the source came from; any frontend that emits
//       GLSL can call this.
//
// Error reporting routes through LoggingProvider. glslang's info log is
// parsed so messages show up as `path:line: text` rather than a raw dump.
//
// Hot reload is intentionally not implemented in this step -- the pipeline
// cache (step 6) is the invalidation consumer and doesn't exist yet. Adding
// hot reload later is additive: a file watcher calls back into LoadFromFile
// and publishes handle-changed events that VulkanPipelineCache subscribes
// to. The handle type does not need to change.
//=============================================================================
enum class ShaderStage : uint8_t
{
    Vertex,
    Fragment,
    Compute,
};

struct ShaderHandle
{
    uint32_t Id = 0;

    [[nodiscard]] bool IsValid() const { return Id != 0; }
    bool operator==(const ShaderHandle&) const = default;
};

class VulkanShaderCache : public IService
{
public:
    VulkanShaderCache(LoggingProvider& logging, VulkanDeviceService& device);
    ~VulkanShaderCache() override;

    VulkanShaderCache(const VulkanShaderCache&) = delete;
    VulkanShaderCache& operator=(const VulkanShaderCache&) = delete;
    VulkanShaderCache(VulkanShaderCache&&) = delete;
    VulkanShaderCache& operator=(VulkanShaderCache&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // -- Ingestion ----------------------------------------------------------

    [[nodiscard]] ShaderHandle LoadFromFile(const std::filesystem::path& path, ShaderStage stage);

    [[nodiscard]] ShaderHandle CompileFromSource(std::string_view source,
                                                 ShaderStage stage,
                                                 const char* debugName = nullptr);

    void Destroy(ShaderHandle handle);

    // -- Accessors ----------------------------------------------------------

    [[nodiscard]] VkShaderModule GetModule(ShaderHandle handle) const;
    [[nodiscard]] ShaderStage GetStage(ShaderHandle handle) const;

private:
    struct ShaderEntry
    {
        VkShaderModule Module = VK_NULL_HANDLE;
        ShaderStage Stage = ShaderStage::Vertex;
        uint32_t Generation = 0;
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    bool Valid = false;

    std::vector<ShaderEntry> Entries;
    std::vector<uint32_t> FreeSlots;

    [[nodiscard]] ShaderEntry* Resolve(ShaderHandle handle);
    [[nodiscard]] const ShaderEntry* Resolve(ShaderHandle handle) const;
    [[nodiscard]] ShaderHandle MakeHandle(uint32_t index, uint32_t generation) const;

    // Compile GLSL -> SPIR-V via glslang. `debugLabel` is used for log messages
    // only (source file path, or a synthetic name for in-memory sources).
    [[nodiscard]] bool CompileGlsl(std::string_view source,
                                   ShaderStage stage,
                                   const char* debugLabel,
                                   std::vector<uint32_t>& outSpirv);

    [[nodiscard]] ShaderHandle CreateModuleFromSpirv(const std::vector<uint32_t>& spirv,
                                                     ShaderStage stage,
                                                     const char* debugLabel);
};
