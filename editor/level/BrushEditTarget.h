#pragma once

#include "../meshedit/IMeshEditTarget.h"

class LevelDocument;
class LevelScene;

class BrushEditTarget : public IMeshEditTarget
{
public:
    BrushEditTarget(LevelScene& scene, LevelDocument& document);

    [[nodiscard]] std::optional<MeshEditTargetMesh> Resolve(EntityId entity) const override;
    [[nodiscard]] std::unique_ptr<ICommand> MakeEditCommand(EntityId entity,
                                                            BrushMesh before,
                                                            BrushMesh after) override;

private:
    LevelScene& Scene;
    LevelDocument& Document;
};
