#pragma once

#include <RmlUi/Core/Dictionary.h>

#include <render/ui/UiDrawFrame.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

struct CompiledGradient
{
    UiGradient Gradient;
    std::vector<std::byte> RampPixels;
};

[[nodiscard]] std::optional<UiGradientShape> GradientShapeForShader(std::string_view shaderName);

[[nodiscard]] std::optional<CompiledGradient> CompileGradient(UiGradientShape shape,
                                                              const Rml::Dictionary& parameters);
