#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// The scenes that still name a clip on the retired clip player, and their
// conversion to one-layer rigs under animation/migrated/.
class AnimationMigrationProperties final : public IPropertySource
{
public:
    explicit AnimationMigrationProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return false; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Gone; }
    void CancelEdit(std::string_view) override {}
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    // Scans the project once, the first time it is shown.
    void ScanOnce();

private:
    AnimationPreviewWorkspace& Workspace;
    bool Scanned = false;
    std::string Status;
};
