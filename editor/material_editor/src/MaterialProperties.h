#pragma once

#include "properties/PropertySource.h"

#include <assets/material/MaterialFormat.h>

#include <optional>
#include <string>

class AssetRegistry;
class MaterialDocumentSet;

// The open material's parameters as the shading model reads them: a drag
// previews on the material and is one step when it lets go.
class MaterialProperties final : public IPropertySource
{
public:
    MaterialProperties(MaterialDocumentSet& tabs, const AssetRegistry& registry) : Tabs(tabs), Registry(registry) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view key) override;
    [[nodiscard]] std::vector<std::string> Candidates(std::string_view key) const override;

private:
    MaterialDocumentSet& Tabs;
    const AssetRegistry& Registry;
    // What the material held when the edit in flight began.
    std::optional<MaterialDescription> Baseline;
};
