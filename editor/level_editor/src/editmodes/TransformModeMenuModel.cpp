#include "TransformModeMenuModel.h"

#include "TransformModeItems.h"
#include "ManipulatorSession.h"

int TransformModeMenuModel::Count() const
{
    return static_cast<int>(kTransformModeItems.size());
}

IRadialMenuModel::MenuItem TransformModeMenuModel::Item(int index) const
{
    if (index < 0 || index >= Count())
        return {};
    return kTransformModeItems[static_cast<std::size_t>(index)].Choice;
}

int TransformModeMenuModel::ActiveIndex() const
{
    const ManipulatorSession* session = SessionResolver ? SessionResolver() : nullptr;
    if (session == nullptr)
        return -1;
    const TransformMode effective = session->EffectiveMode();
    for (std::size_t i = 0; i < kTransformModeItems.size(); ++i)
        if (kTransformModeItems[i].Mode == effective)
            return static_cast<int>(i);
    return -1;
}

void TransformModeMenuModel::Select(int index, int /*variant*/)
{
    ManipulatorSession* session = SessionResolver ? SessionResolver() : nullptr;
    if (session == nullptr || index < 0 || index >= Count())
        return;
    session->SetTransformMode(kTransformModeItems[static_cast<std::size_t>(index)].Mode);
}
