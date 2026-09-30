#pragma once

#include "tools/RadialMenuModel.h"

#include <functional>

class ManipulatorSession;

// The transform gizmo modes as a radial menu, over the manipulator session:
// the entries are the shared mode table, each fanned with the spaces it can
// drag along, so a mode and its space are one gesture. The entry in effect is
// the mode the session is presently driving (EffectiveMode), its variant the
// space. The session is resolved at call time, because the workspace stands
// it up after the menu is built.
class TransformModeMenuModel final : public IRadialMenuModel
{
public:
    explicit TransformModeMenuModel(std::function<ManipulatorSession*()> session) : SessionResolver(std::move(session)) {}

    [[nodiscard]] int Count() const override;
    [[nodiscard]] MenuItem Item(int index) const override;
    [[nodiscard]] int ActiveIndex() const override;
    [[nodiscard]] std::span<const MenuItem> Variants(int index) const override;
    [[nodiscard]] int ActiveVariant(int index) const override;
    void Select(int index, int variant) override;

private:
    std::function<ManipulatorSession*()> SessionResolver;
};
