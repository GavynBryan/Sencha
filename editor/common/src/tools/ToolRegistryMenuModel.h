#pragma once

#include "RadialMenuModel.h"

class ToolRegistry;

// The registered tools as a radial menu: one entry per tool in registry
// order, a tool's variants as the entry's, and a choice made through the
// registry's own entry rules (ToolRegistry::Activate / SelectVariant).
class ToolRegistryMenuModel final : public IRadialMenuModel
{
public:
    explicit ToolRegistryMenuModel(ToolRegistry& tools) : Tools(tools) {}

    [[nodiscard]] int Count() const override;
    [[nodiscard]] MenuItem Item(int index) const override;
    [[nodiscard]] int ActiveIndex() const override;
    [[nodiscard]] std::span<const MenuItem> Variants(int index) const override;
    [[nodiscard]] int ActiveVariant(int index) const override;
    void Select(int index, int variant) override;

private:
    ToolRegistry& Tools;
};
