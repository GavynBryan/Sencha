#pragma once

#include "properties/PropertySource.h"

#include <string>

class AnimationPreviewWorkspace;

// The scenes that still name a clip on the retired clip player, and their
// conversion to one-layer rigs under animation/migrated/.
class AnimationMigrationProperties final : public ReadOnlyPropertySource
{
public:
    explicit AnimationMigrationProperties(AnimationPreviewWorkspace& workspace) : Workspace(workspace) {}

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

    // Scans the project once, the first time it is shown.
    void ScanOnce();

private:
    AnimationPreviewWorkspace& Workspace;
    bool Scanned = false;
    std::string Status;
};
