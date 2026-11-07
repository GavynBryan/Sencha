#pragma once

#include <render/ShaderCatalog.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::size_t kMaterialShaderParamCount = 2;

struct MaterialShaderId
{
    std::uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(MaterialShaderId, MaterialShaderId) = default;
};

struct MaterialShaderDesc
{
    std::string_view Name;
    std::span<const std::uint32_t> FragmentSpirv;
};

struct MaterialShaderType
{
    std::string Name;
    std::vector<std::uint32_t> FragmentSpirv;
};

// Docs: docs/renderer/material-shaders.md.
class MaterialShaderCatalog : public ShaderCatalog<MaterialShaderId, MaterialShaderType>
{
public:
    MaterialShaderCatalog();

    [[nodiscard]] MaterialShaderId Register(const MaterialShaderDesc& desc);

    // Unique per catalog, so a replaced catalog's ids are not mistaken for this one's.
    [[nodiscard]] std::uint64_t Stamp() const { return Stamp_; }

private:
    std::uint64_t Stamp_;
};
