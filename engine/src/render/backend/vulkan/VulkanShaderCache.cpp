#include <render/backend/vulkan/VulkanShaderCache.h>

#include <render/backend/vulkan/VulkanDeviceService.h>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include <fstream>
#include <sstream>
#include <system_error>

namespace
{
    // Process-wide glslang init is ref-counted so multiple VulkanShaderCache
    // instances (tests, tools) don't stomp on each other.
    int g_glslangRefCount = 0;

    constexpr uint32_t kIndexBits = 20;
    constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;
    constexpr uint32_t kMaxGeneration = (1u << (32u - kIndexBits)) - 1u;

    uint32_t DecodeIndex(uint32_t id) { return id & kIndexMask; }
    uint32_t DecodeGeneration(uint32_t id) { return id >> kIndexBits; }

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

    bool ReadTextFile(const std::filesystem::path& path, std::string& out)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        std::ostringstream ss;
        ss << file.rdbuf();
        out = ss.str();
        return true;
    }

    bool ReadSpirvFile(const std::filesystem::path& path, std::vector<uint32_t>& out)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        const std::streamsize bytes = file.tellg();
        if (bytes <= 0 || (bytes % sizeof(uint32_t)) != 0) return false;
        file.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(bytes) / sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(out.data()), bytes);
        return file.good() || file.eof();
    }

    bool WriteSpirvFile(const std::filesystem::path& path, const std::vector<uint32_t>& spirv)
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(reinterpret_cast<const char*>(spirv.data()),
                   static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
        return file.good();
    }

    bool SpirvCacheIsFresh(const std::filesystem::path& source,
                           const std::filesystem::path& cache)
    {
        std::error_code ec;
        if (!std::filesystem::exists(cache, ec)) return false;
        const auto cacheTime = std::filesystem::last_write_time(cache, ec);
        if (ec) return false;
        const auto sourceTime = std::filesystem::last_write_time(source, ec);
        if (ec) return false;
        return cacheTime >= sourceTime;
    }
}

VulkanShaderCache::VulkanShaderCache(LoggingProvider& logging, VulkanDeviceService& device)
    : Log(logging.GetLogger<VulkanShaderCache>())
    , Device(device.GetDevice())
{
    if (!device.IsValid())
    {
        Log.Error("Cannot create VulkanShaderCache: VulkanDeviceService not valid");
        return;
    }

    if (g_glslangRefCount++ == 0)
    {
        glslang::InitializeProcess();
    }

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

    if (Valid && --g_glslangRefCount == 0)
    {
        glslang::FinalizeProcess();
    }
}

ShaderHandle VulkanShaderCache::MakeHandle(uint32_t index, uint32_t generation) const
{
    ShaderHandle h;
    h.Id = (generation << kIndexBits) | (index & kIndexMask);
    return h;
}

VulkanShaderCache::ShaderEntry* VulkanShaderCache::Resolve(ShaderHandle handle)
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Module == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

const VulkanShaderCache::ShaderEntry* VulkanShaderCache::Resolve(ShaderHandle handle) const
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    const auto& entry = Entries[index];
    if (entry.Generation != gen || entry.Module == VK_NULL_HANDLE) return nullptr;
    return &entry;
}

ShaderHandle VulkanShaderCache::LoadFromFile(const std::filesystem::path& path, ShaderStage stage, bool optimize)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        Log.Error("LoadFromFile: source does not exist: {}", path.generic_string());
        return {};
    }

    std::filesystem::path cachePath = path;
    cachePath += ".spv";
    const std::string label = path.filename().string();

    std::vector<uint32_t> spirv;

    if (SpirvCacheIsFresh(path, cachePath) && ReadSpirvFile(cachePath, spirv))
    {
        return CreateModuleFromSpirv(spirv, stage, label.c_str());
    }

    std::string source;
    if (!ReadTextFile(path, source))
    {
        Log.Error("LoadFromFile: failed to read source: {}", label);
        return {};
    }

    if (!CompileGlsl(source, stage, label.c_str(), spirv, optimize))
    {
        return {};
    }

    if (!WriteSpirvFile(cachePath, spirv))
    {
        Log.Warn("LoadFromFile: failed to write spirv cache: {}", cachePath.string());
    }

    return CreateModuleFromSpirv(spirv, stage, label.c_str());
}

ShaderHandle VulkanShaderCache::CompileFromSource(std::string_view source,
                                                   ShaderStage stage,
                                                   const char* debugName,
                                                   bool optimize)
{
    const char* label = debugName != nullptr ? debugName : "<inline>";
    std::vector<uint32_t> spirv;
    if (!CompileGlsl(source, stage, label, spirv, optimize))
    {
        return {};
    }
    return CreateModuleFromSpirv(spirv, stage, label);
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
    const int lengths[1] = { static_cast<int>(source.size()) };
    const char* names[1] = { debugLabel };
    shader.setStringsWithLengthsAndNames(sources, lengths, names, 1);

    shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

    const TBuiltInResource* resources = GetDefaultResources();
    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(resources, 450, false, messages))
    {
        Log.Error("Shader parse failed ({}): {}", debugLabel, shader.getInfoLog());
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
    {
        Log.Error("Shader link failed ({}): {}", debugLabel, program.getInfoLog());
        return false;
    }

    glslang::SpvOptions spvOptions;
    spvOptions.disableOptimizer = !optimize;
    spvOptions.generateDebugInfo = !optimize;
    glslang::GlslangToSpv(*program.getIntermediate(lang), outSpirv, &spvOptions);

    if (outSpirv.empty())
    {
        Log.Error("Shader SPIR-V emission produced empty output ({})", debugLabel);
        return false;
    }

    return true;
}

ShaderHandle VulkanShaderCache::CreateModuleFromSpirv(const std::vector<uint32_t>& spirv,
                                                       ShaderStage stage,
                                                       const char* debugLabel)
{
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = spirv.size() * sizeof(uint32_t);
    info.pCode = spirv.data();

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(Device, &info, nullptr, &module);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateShaderModule failed for {} (code {})", debugLabel, static_cast<int>(result));
        return {};
    }

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
            Log.Error("VulkanShaderCache slot capacity exhausted");
            vkDestroyShaderModule(Device, module, nullptr);
            return {};
        }
        Entries.emplace_back();
    }

    auto& entry = Entries[index];
    entry.Module = module;
    entry.Stage = stage;
    entry.Generation = entry.Generation + 1;
    if (entry.Generation == 0 || entry.Generation > kMaxGeneration)
    {
        entry.Generation = 1;
    }

    return MakeHandle(index, entry.Generation);
}

void VulkanShaderCache::Destroy(ShaderHandle handle)
{
    auto* entry = Resolve(handle);
    if (entry == nullptr) return;

    vkDestroyShaderModule(Device, entry->Module, nullptr);
    entry->Module = VK_NULL_HANDLE;

    const uint32_t index = DecodeIndex(handle.Id);
    FreeSlots.push_back(index);
}

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
