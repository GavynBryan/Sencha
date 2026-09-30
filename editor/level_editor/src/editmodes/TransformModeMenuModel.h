#pragma once

#include "tools/RadialMenuModel.h"

#include <functional>

class ManipulatorSession;

// The transform gizmo modes as a radial menu, over the manipulator session:
// the entries are the shared mode table, the one in effect is the mode the
// session is presently driving (EffectiveMode, what the toolbar highlights),
// and a choice requests that mode. The session is resolved at call time, as
// the toolbar resolves it, because the workspace stands it up after the menu
// is built.
class TransformModeMenuModel final : public IRadialMenuModel
{
public:
    explicit TransformModeMenuModel(std::function<ManipulatorSession*()> session) : SessionResolver(std::move(session)) {}

    [[nodiscard]] int Count() const override;
    [[nodiscard]] MenuItem Item(int index) const override;
    [[nodiscard]] int ActiveIndex() const override;
    void Select(int index, int variant) override;

private:
    std::function<ManipulatorSession*()> SessionResolver;
};
