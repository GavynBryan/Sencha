#pragma once

#include "properties/PropertySource.h"

#include <functional>
#include <string>

class ToolRegistry;

//=============================================================================
// ToolParameterSource
// The tool in hand's parameters as a property source. Tool settings have no
// transaction: preview sets, commit keeps, cancel restores the starting value.
//=============================================================================
class ToolParameterSource final : public IPropertySource
{
public:
    // `tools` answers the registry in use now; a workspace rebuilds its tools
    // when the document under them changes.
    explicit ToolParameterSource(std::function<ToolRegistry*()> tools) : Tools(std::move(tools)) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;

private:
    PropertyEditResult Set(std::string_view key, std::string_view value);

    std::function<ToolRegistry*()> Tools;
    std::string EditKey;
    std::string EditBegan;
};
