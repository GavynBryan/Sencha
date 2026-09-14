#pragma once

#include <assets/cook/AssetImporter.h>

//=============================================================================
// UI package cook. Dev-only -- compiled under SENCHA_ENABLE_COOK, never shipped.
//
// A .rml source and every stylesheet it pulls in become one .sui: the markup,
// the stylesheets, the table of fonts and textures the document needs, and the
// constructs the cooker noticed are outside the supported rendering profile.
//
// The stylesheets are read through ImportInput::Sources and listed in
// ImportResult::AdditionalSources, which is what makes editing a shared theme
// recook every document that imports it. Reading them any other way would leave
// them outside the freshness hash, and a stale build cache is a worse bug than a
// missing feature because nothing about it looks wrong.
//
// Only the root .rml is claimed. A .rcss is never a cooked asset in its own
// right -- it has no runtime identity, and a document that imports it carries a
// copy. That is deliberate: a package opens with no filesystem beneath it, so
// everything it needs has to be inside it.
//=============================================================================
class UiPackageImporter final : public IAssetImporter
{
public:
    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
};
