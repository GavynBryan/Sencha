#pragma once

#include "properties/PropertySource.h"

#include <assets/cook/TextureImportSettings.h>

#include <functional>
#include <optional>
#include <string>

// A source texture's import settings beside what its cook holds; a change is
// saved and cooked at once, and the cook is read back rather than assumed.
class TextureImportProperties final : public CommitOnlyPropertySource
{
public:
    struct Shown
    {
        std::string Texture;
        // Nothing when the texture has no source file to set anything for.
        std::optional<TextureImportSettings> Settings;
        // What the cook holds now, in words.
        std::string Cooked;
        std::string Problem = {};
    };
    // Saves and cooks; says what the cook holds after, or why it could not.
    using ApplyFn = std::function<bool(const std::string& texture, const TextureImportSettings& settings,
                                       std::string& cooked, std::string& error)>;
    using CreateMaterialFn = std::function<void(const std::string& texture)>;

    TextureImportProperties(ApplyFn apply, CreateMaterialFn createMaterial)
        : Apply(std::move(apply))
        , CreateMaterial(std::move(createMaterial))
    {
    }

    void Show(Shown shown) { Current = std::move(shown); }
    [[nodiscard]] const std::string& Texture() const { return Current.Texture; }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view key) override;
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    ApplyFn Apply;
    CreateMaterialFn CreateMaterial;
    Shown Current;
};
