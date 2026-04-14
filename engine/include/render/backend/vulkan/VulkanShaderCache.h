#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

class VulkanDeviceService;

//=============================================================================
// VulkanShaderCache
//
// Owns every VkShaderModule the engine creates.  Modules are referenced by
// opaque ShaderHandle values with generational validation (same slot-array
// pattern as VulkanBufferService / VulkanImageService).
//
// ── Runtime contract ──────────────────────────────────────────────────────
//
// The production ingestion path is SPIR-V only:
//
//   CreateModuleFromSpirv(words, wordCount, debugName)
//       Accepts a pre-compiled SPIR-V blob passed as a uint32_t pointer.
//       Engine-internal shaders use this exclusively: their SPIR-V is baked
//       into the binary as constexpr uint32_t[] arrays by the offline build
//       pipeline (cmake/SenchaShaders.cmake + cmake/EmbedSpirv.cmake).
//       No file I/O.  No compiler dependency.  Zero startup cost.
//
//   LoadSpirv(path, stage, debugName)
//       Reads a pre-compiled .spv file from disk and calls
//       CreateModuleFromSpirv.  Intended for game shaders during development
//       (loaded from the build output directory) and for the future runtime
//       asset loader.  No GLSL compiler involved.
//
// ── Hot-reload path (SENCHA_ENABLE_HOT_RELOAD only) ──────────────────────
//
// When built with -DSENCHA_ENABLE_HOT_RELOAD=ON (never in release):
//
//   LoadFromFile(path, stage)
//       Reads a GLSL source file, compiles it via glslang, writes a .spv
//       side-car cache next to the source (invalidated by mtime), and calls
//       CreateModuleFromSpirv.  The file watcher calls this to implement
//       live shader reload during development.
//
//   CompileFromSource(source, stage, debugName)
//       Compiles an in-memory GLSL string.  Reserved for runtime-generated
//       shaders (node/material graph editors, test fixtures).
//
// glslang is linked only when SENCHA_ENABLE_HOT_RELOAD is defined.  Shipping
// binaries have zero compiler dependency.
//
// ── Pipeline cache interaction ────────────────────────────────────────────
//
// Hot reload: after replacing a module via LoadFromFile the caller is
// responsible for broadcasting the change to VulkanPipelineCache so stale
// VkPipeline entries referencing the old ShaderHandle are invalidated and
// re-created next frame.  See docs/shaders.md §Hot Reload for the wiring.
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

    VulkanShaderCache(const VulkanShaderCache&)            = delete;
    VulkanShaderCache& operator=(const VulkanShaderCache&) = delete;
    VulkanShaderCache(VulkanShaderCache&&)                 = delete;
    VulkanShaderCache& operator=(VulkanShaderCache&&)      = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // ── Production ingestion (always available) ───────────────────────────

    // Create a module from a pre-compiled SPIR-V blob in memory.
    // `words` must remain valid only for the duration of this call; the driver
    // makes its own copy.  `wordCount` is the number of uint32_t elements
    // (NOT byte count).
    [[nodiscard]] ShaderHandle CreateModuleFromSpirv(const uint32_t* words,
                                                      uint32_t wordCount,
                                                      const char* debugName = nullptr);

    // Read a pre-compiled .spv file from disk and create a module.
    // No GLSL compilation.  Returns an invalid handle if the file is missing
    // or malformed.
    [[nodiscard]] ShaderHandle LoadSpirv(const std::filesystem::path& path,
                                          ShaderStage stage,
                                          const char* debugName = nullptr);

    // ── Hot-reload ingestion (SENCHA_ENABLE_HOT_RELOAD builds only) ───────

#ifdef SENCHA_ENABLE_HOT_RELOAD
    // Compile a GLSL source file to SPIR-V via glslang.  An on-disk .spv
    // side-car (foo.frag -> foo.frag.spv) is written next to the source and
    // reused if it is newer than the source.  Used by the file watcher for
    // live shader reload.
    [[nodiscard]] ShaderHandle LoadFromFile(const std::filesystem::path& path,
                                             ShaderStage stage,
                                             bool optimize = false);

    // Compile an in-memory GLSL string.  No disk cache.  Intended for
    // runtime-generated shaders (material graph, tests).
    [[nodiscard]] ShaderHandle CompileFromSource(std::string_view source,
                                                  ShaderStage stage,
                                                  const char* debugName = nullptr,
                                                  bool optimize = false);
#endif

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Destroy(ShaderHandle handle);

    // ── Accessors ─────────────────────────────────────────────────────────

    [[nodiscard]] VkShaderModule GetModule(ShaderHandle handle) const;
    [[nodiscard]] ShaderStage    GetStage(ShaderHandle handle)  const;

private:
    struct ShaderEntry
    {
        VkShaderModule Module     = VK_NULL_HANDLE;
        ShaderStage    Stage      = ShaderStage::Vertex;
        uint32_t       Generation = 0;
    };

    Logger&   Log;
    VkDevice  Device = VK_NULL_HANDLE;
    bool      Valid  = false;

    std::vector<ShaderEntry> Entries;
    std::vector<uint32_t>    FreeSlots;

    [[nodiscard]] ShaderEntry*       Resolve(ShaderHandle handle);
    [[nodiscard]] const ShaderEntry* Resolve(ShaderHandle handle) const;
    [[nodiscard]] ShaderHandle       MakeHandle(uint32_t index, uint32_t generation) const;

    // Allocate a slot, register the module, and return a handle.
    [[nodiscard]] ShaderHandle RegisterEntry(VkShaderModule module,
                                              ShaderStage stage,
                                              const char* debugLabel);

#ifdef SENCHA_ENABLE_HOT_RELOAD
    [[nodiscard]] bool CompileGlsl(std::string_view source,
                                    ShaderStage stage,
                                    const char* debugLabel,
                                    std::vector<uint32_t>& outSpirv,
                                    bool optimize);

    static bool ReadTextFile(const std::filesystem::path& path, std::string& out);
    static bool ReadSpirvFile(const std::filesystem::path& path, std::vector<uint32_t>& out);
    static bool WriteSpirvFile(const std::filesystem::path& path,
                                const std::vector<uint32_t>& spirv);
    static bool SpirvCacheIsFresh(const std::filesystem::path& source,
                                   const std::filesystem::path& cache);
#endif
};
