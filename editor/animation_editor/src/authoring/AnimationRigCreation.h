#pragma once

#include "authoring/AnimationRigRecipe.h"

#include "properties/PropertySource.h"

#include <functional>
#include <string>

class AnimationPreviewWorkspace;

// A new rig drafted: name, tier, clips on one skeleton and a character's upper
// body joint; Create writes its documents and opens it, telling `created`.
class AnimationRigCreation final : public CommitOnlyPropertySource
{
public:
    AnimationRigCreation(AnimationPreviewWorkspace& workspace, std::function<void()> created)
        : Workspace(workspace)
        , Created(std::move(created))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    PropertyEditResult Act(std::string_view key, std::string_view verb, std::string_view choice) override;

private:
    // The skeleton every chosen clip must pose: the first chosen clip's.
    [[nodiscard]] std::string ChosenSkeleton() const;
    [[nodiscard]] std::string SkeletonOf(const std::string& clip) const;

    AnimationPreviewWorkspace& Workspace;
    std::function<void()> Created;
    AnimationRigRecipe Recipe;
    std::string Error;
};
