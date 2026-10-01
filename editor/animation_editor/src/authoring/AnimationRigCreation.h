#pragma once

#include "authoring/AnimationRigRecipe.h"

#include "properties/PropertySource.h"

#include <functional>
#include <string>

class AnimationPreviewWorkspace;

//=============================================================================
// AnimationRigCreation
//
// A new rig, drafted: its name, its tier (prop, simple, character), the clips
// it plays -- the first idles, and every clip must pose the first one's
// skeleton -- and, for a character, the joint its upper body starts at.
// Create writes the rig's documents and opens it; `created` hears it opened.
//=============================================================================
class AnimationRigCreation final : public IPropertySource
{
public:
    AnimationRigCreation(AnimationPreviewWorkspace& workspace, std::function<void()> created)
        : Workspace(workspace)
        , Created(std::move(created))
    {
    }

    [[nodiscard]] std::vector<PropertyNode> Nodes() const override;
    bool BeginEdit(std::string_view) override { return true; }
    PropertyEditResult PreviewEdit(std::string_view, std::string_view) override { return PropertyEditResult::Unchanged; }
    PropertyEditResult CommitEdit(std::string_view key, std::string_view value) override;
    void CancelEdit(std::string_view) override {}
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
