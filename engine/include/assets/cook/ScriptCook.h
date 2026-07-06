#pragma once

#include <assets/cook/AssetImporter.h>

#include <string>

//=============================================================================
// ScriptImporter (docs/plans/t-language-v1-spec.md, section E milestone 4)
//
// Cooks T sources: .t compiles to a .tbc bytecode container under .cooked/,
// keeping the source's virtual path (Decision B). Dev-only, like every
// importer.
//
// T sources are not self-contained: `import "lib.t"` pulls sibling files.
// The importer therefore takes the assets root (the Blender importer set the
// filesystem precedent) and reports the import closure through
// ComputeDependencyHash so an edited library recooks its dependents.
//=============================================================================
class ScriptImporter final : public IAssetImporter
{
public:
    explicit ScriptImporter(std::string assetsRoot);

    [[nodiscard]] std::vector<std::string_view> SourceExtensions() const override;
    [[nodiscard]] ImportResult Import(const ImportInput& input,
                                      ICookOutputWriter& output) override;
    [[nodiscard]] uint64_t ComputeDependencyHash(const ImportInput& input) const override;

private:
    std::string AssetsRoot;
};
