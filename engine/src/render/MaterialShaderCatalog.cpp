#include <render/MaterialShaderCatalog.h>

#include <graphics/SpirvInterface.h>
#include <render/MeshPushConstants.h>
#include <shaders/kMeshForwardVertSpv.h>

#include <algorithm>
#include <atomic>

namespace
{
std::atomic<std::uint64_t> NextStamp{ 1 };

std::string InterfaceMismatch(std::span<const std::uint32_t> spirv)
{
    const SpirvInterface shader = ReadSpirvInterface(spirv);
    if (!shader.Valid)
        return "it is not SPIR-V";
    static const std::set<std::uint32_t> written =
        ReadSpirvInterface({ kMeshForwardVertSpv, kMeshForwardVertSpvWordCount }).Outputs;
    if (!std::ranges::includes(written, shader.Inputs))
        return "it reads an input the mesh vertex stage does not write";
    if (shader.LastPushMemberOffset.value_or(0) >= sizeof(MeshPushConstants))
        return "its push constants run past the mesh block";
    return {};
}
} // namespace

MaterialShaderCatalog::MaterialShaderCatalog()
    : Stamp_(NextStamp++)
{
}

MaterialShaderId MaterialShaderCatalog::Register(const MaterialShaderDesc& desc)
{
    return Add("material shader",
               MaterialShaderType{ .Name = std::string(desc.Name),
                                   .FragmentSpirv = { desc.FragmentSpirv.begin(), desc.FragmentSpirv.end() } },
               InterfaceMismatch(desc.FragmentSpirv));
}
