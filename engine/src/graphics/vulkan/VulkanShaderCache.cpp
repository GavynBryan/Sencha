#include <graphics/vulkan/VulkanShaderCache.h>

#include <graphics/vulkan/VulkanDeviceService.h>

#ifdef SENCHA_ENABLE_HOT_RELOAD
#  include <glslang/Public/ResourceLimits.h>
#  include <glslang/Public/ShaderLang.h>
#  include <SPIRV/GlslangToSpv.h>
#  include <fstream>
#  include <sstream>
#  include <system_error>
#endif

#include <fstream>

namespace
{
    constexpr uint32_t kIndexBits  = 20u;
    constexpr uint32_t kIndexMask  = (1u << kIndexBits) - 1u;
    constexpr uint32_t kMaxGen     = (1u << (32u - kIndexBits)) - 1u;

    uint32_t DecodeIndex(uint32_t id)      { return id & kIndexMask; }
    uint32_t DecodeGeneration(uint32_t id) { return id >> kIndexBits; }

#ifdef SENCHA_ENABLE_HOT_RELOAD
    // Process-wide ref-count for glslang initialisation.
    // Multiple VulkanShaderCache instances (unit tests, tools) share the
    // single glslang process state safely.
    int g_glslangRefCount = 0;

    EShLanguage ToGlslangStage(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::Vertex:   return EShLangVertex;
        case ShaderStage::Fragment: return EShLangFragment;
        case ShaderStage::Compute:  return EShLangCompute;
        }
        return EShLangVertex;
    }
#endif
} // namespace

// ── Construction / destruction ────────────────────────────────────────────────

VulkanShaderCache::VulkanShaderCache(LoggingProvider& logging,
                                       VulkanDeviceService& device)
    : Log(logging.GetLogger<VulkanShaderCache>())
    , Device(device.GetDevice())
{
    if (!device.IsValid())
    {
        Log.Error("VulkanShaderCache: VulkanDeviceService is not valid");
        return;
    }

#ifdef SENCHA_ENABLE_HOT_RELOAD
    if (g_glslangRefCount++ == 0)
    {
        glslang::InitializeProcess();
    }
#endif

    // Slot 0 is reserved as the "null" index so that Id==0 means invalid.
    Entries.emplace_back();
    Valid = true;
}

VulkanShaderCache::~VulkanShaderCache()
{
    for (size_t i = 1; i < Entries.size(); ++i)
    {
        auto& entry = Entries[i];
        if (entry.Module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(Device, entry.Module, nullptr);
            entry = {};
        }
    }

#ifdef SENCHA_ENABLE_HOT_RELOAD
    if (Valid && --g_glslangRefCount == 0)
    {
        glslang::FinalizeProcess();
    }
#endif
}

// ── Handle helpers ────────────────────────────────────────────────────────────

ShaderHandle VulkanShaderCache::MakeHandle(uint32_t index,
                                            uint32_t generation) const
{
    ShaderHandle h;
    h.Id = (generation << kIndexBits) | (index & kIndexMask);
    return h;
}

VulkanShaderCache::ShaderEntry* VulkanShaderCache::Resolve(ShaderHandle handle)
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen   = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Module == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

const VulkanShaderCache::ShaderEntry*
VulkanShaderCache::Resolve(ShaderHandle handle) const
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen   = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    const auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Module == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

// Shared slot management for every ingestion path.
ShaderHandle VulkanShaderCache::RegisterEntry(VkShaderModule module,
                                               ShaderStage stage,
                                               const char* /*debugLabel*/)
{
    uint32_t index;
    if (!FreeSlots.empty())
    {
        index = FreeSlots.back();
        FreeSlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(Entries.size());
        if (index > kIndexMask)
        {
            Log.Error("VulkanShaderCache: slot capacity exhausted");
            vkDestroyShaderModule(Device, module, nullptr);
            return {};
        }
        Entries.emplace_back();
    }

    auto& entry = Entries[index];
    entry.Module = module;
    entry.Stage  = stage;
    entry.Generation++;
    if (entry.Generation == 0 || entry.Generation > kMaxGen)
        entry.Generation = 1;

    return MakeHandle(index, entry.Generation);
}

// ── Production ingestion ──────────────────────────────────────────────────────

ShaderHandle VulkanShaderCache::CreateModuleFromSpirv(const uint32_t* words,
                                                       uint32_t wordCount,
                                                       const char* debugName)
{
    if (!Valid)
    {
        Log.Error("CreateModuleFromSpirv: cache is not valid");
        return {};
    }
    if (words == nullptr || wordCount == 0)
    {
        Log.Error("CreateModuleFromSpirv: null or empty SPIR-V ({})",
                   debugName ? debugName : "<unnamed>");
        return {};
    }

    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = static_cast<size_t>(wordCount) * sizeof(uint32_t);
    info.pCode    = words;

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(Device, &info, nullptr, &module);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateShaderModule failed for '{}' (VkResult {})",
                   debugName ? debugName : "<unnamed>",
                   static_cast<int>(result));
        return {};
    }

    // Stage is not carried in the SPIR-V header in a way we parse here;
    // callers of the embedded-array path don't need it for pipeline creation
    // (VulkanPipelineCache reads the stage from the desc, not the module).
    // We default to Vertex; hot-reload and LoadSpirv pass the correct stage.
    return RegisterEntry(module, ShaderStage::Vertex, debugName);
}

ShaderHandle VulkanShaderCache::LoadSpirv(const std::filesystem::path& path,
                                           ShaderStage stage,
                                           const char* debugName)
{
    if (!Valid)
    {
        Log.Error("LoadSpirv: cache is not valid");
        return {};
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        Log.Error("LoadSpirv: cannot open '{}'", path.generic_string());
        return {};
    }

    const std::streamsize bytes = file.tellg();
    if (bytes <= 0 || (bytes % static_cast<std::streamsize>(sizeof(uint32_t))) != 0)
    {
        Log.Error("LoadSpirv: '{}' has invalid size ({} bytes)",
                   path.generic_string(), bytes);
        return {};
    }

    file.seekg(0);
    std::vector<uint32_t> spirv(static_cast<size_t>(bytes) / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(spirv.data()), bytes);
    if (!file.good() && !file.eof())
    {
        Log.Error("LoadSpirv: read error on '{}'", path.generic_string());
        return {};
    }

    const char* label = debugName ? debugName : path.filename().string().c_str();
    ShaderHandle handle = CreateModuleFromSpirv(spirv.data(),
                                                 static_cast<uint32_t>(spirv.size()),
                                                 label);
    if (handle.IsValid())
    {
        // Patch the stage now that we know it.
        if (auto* entry = Resolve(handle))
            entry->Stage = stage;
    }
    return handle;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void VulkanShaderCache::Destroy(ShaderHandle handle)
{
    auto* entry = Resolve(handle);
    if (entry == nullptr) return;

    vkDestroyShaderModule(Device, entry->Module, nullptr);
    entry->Module = VK_NULL_HANDLE;

    FreeSlots.push_back(DecodeIndex(handle.Id));
}

// ── Accessors ─────────────────────────────────────────────────────────────────

VkShaderModule VulkanShaderCache::GetModule(ShaderHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->Module : VK_NULL_HANDLE;
}

ShaderStage VulkanShaderCache::GetStage(ShaderHandle handle) const
{
    const auto* entry = Resolve(handle);
    return entry ? entry->Stage : ShaderStage::Vertex;
}

// ── Hot-reload ingestion (SENCHA_ENABLE_HOT_RELOAD only) ─────────────────────

#ifdef SENCHA_ENABLE_HOT_RELOAD

bool VulkanShaderCache::ReadTextFile(const std::filesystem::path& path,
                                      std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

bool VulkanShaderCache::ReadSpirvFile(const std::filesystem::path& path,
                                       std::vector<uint32_t>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const std::streamsize bytes = file.tellg();
    if (bytes <= 0 || (bytes % sizeof(uint32_t)) != 0) return false;
    file.seekg(0);
    out.resize(static_cast<size_t>(bytes) / sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(out.data()), bytes);
    return file.good() || file.eof();
}

bool VulkanShaderCache::WriteSpirvFile(const std::filesystem::path& path,
                                        const std::vector<uint32_t>& spirv)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(spirv.data()),
               static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
    return file.good();
}

bool VulkanShaderCache::SpirvCacheIsFresh(const std::filesystem::path& source,
                                           const std::filesystem::path& cache)
{
    std::error_code ec;
    if (!std::filesystem::exists(cache, ec)) return false;
    const auto cacheTime  = std::filesystem::last_write_time(cache, ec);
    if (ec) return false;
    const auto sourceTime = std::filesystem::last_write_time(source, ec);
    if (ec) return false;
    return cacheTime >= sourceTime;
}

bool VulkanShaderCache::CompileGlsl(std::string_view source,
                                     ShaderStage stage,
                                     const char* debugLabel,
                                     std::vector<uint32_t>& outSpirv,
                                     bool optimize)
{
    const EShLanguage lang = ToGlslangStage(stage);
    glslang::TShader shader(lang);

    const char* sources[1] = { source.data() };
    const int   lengths[1] = { static_cast<int>(source.size()) };
    const char* names[1]   = { debugLabel };
    shader.setStringsWithLengthsAndNames(sources, lengths, names, 1);

    shader.setEnvInput( glslang::EShSourceGlsl, lang,
                        glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan,
                        glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv,
                        glslang::EShTargetSpv_1_6);

    const TBuiltInResource* resources = GetDefaultResources();
    const EShMessages messages =
        static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(resources, 450, false, messages))
    {
        Log.Error("Shader parse failed ({}): {}", debugLabel,
                   shader.getInfoLog());
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
    {
        Log.Error("Shader link failed ({}): {}", debugLabel,
                   program.getInfoLog());
        return false;
    }

    glslang::SpvOptions spvOptions{};
    spvOptions.disableOptimizer = !optimize;
    spvOptions.generateDebugInfo = !optimize;
    glslang::GlslangToSpv(*program.getIntermediate(lang), outSpirv, &spvOptions);

    if (outSpirv.empty())
    {
        Log.Error("Shader SPIR-V emission was empty ({})", debugLabel);
        return false;
    }
    return true;
}

ShaderHandle VulkanShaderCache::LoadFromFile(const std::filesystem::path& path,
                                              ShaderStage stage,
                                              bool optimize)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        Log.Error("LoadFromFile: source not found: {}",
                   path.generic_string());
        return {};
    }

    std::filesystem::path cachePath = path;
    cachePath += ".spv";
    const std::string label = path.filename().string();

    std::vector<uint32_t> spirv;

    if (SpirvCacheIsFresh(path, cachePath) && ReadSpirvFile(cachePath, spirv))
    {
        ShaderHandle h = CreateModuleFromSpirv(
            spirv.data(), static_cast<uint32_t>(spirv.size()), label.c_str());
        if (h.IsValid())
            if (auto* entry = Resolve(h)) entry->Stage = stage;
        return h;
    }

    std::string source;
    if (!ReadTextFile(path, source))
    {
        Log.Error("LoadFromFile: failed to read source: {}", label);
        return {};
    }

    if (!CompileGlsl(source, stage, label.c_str(), spirv, optimize))
        return {};

    if (!WriteSpirvFile(cachePath, spirv))
        Log.Warn("LoadFromFile: could not write SPIR-V cache: {}",
                  cachePath.string());

    ShaderHandle h = CreateModuleFromSpirv(
        spirv.data(), static_cast<uint32_t>(spirv.size()), label.c_str());
    if (h.IsValid())
        if (auto* entry = Resolve(h)) entry->Stage = stage;
    return h;
}

ShaderHandle VulkanShaderCache::CompileFromSource(std::string_view source,
                                                   ShaderStage stage,
                                                   const char* debugName,
                                                   bool optimize)
{
    const char* label = debugName ? debugName : "<inline>";
    std::vector<uint32_t> spirv;
    if (!CompileGlsl(source, stage, label, spirv, optimize))
        return {};

    ShaderHandle h = CreateModuleFromSpirv(
        spirv.data(), static_cast<uint32_t>(spirv.size()), label);
    if (h.IsValid())
        if (auto* entry = Resolve(h)) entry->Stage = stage;
    return h;
}

#endif // SENCHA_ENABLE_HOT_RELOAD
