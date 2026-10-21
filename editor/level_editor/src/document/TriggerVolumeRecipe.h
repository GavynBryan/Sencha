#pragma once

#include "EditorEntityRecipe.h"

class TriggerVolumeRecipe final : public IEditorEntityRecipe
{
public:
    [[nodiscard]] EntitySnapshot Build(const EditorCreateContext& context) const override;
};
