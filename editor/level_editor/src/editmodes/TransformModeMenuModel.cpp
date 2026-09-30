#include "TransformModeMenuModel.h"

#include "TransformModeItems.h"
#include "ManipulatorSession.h"

#include <array>

namespace
{
constexpr std::array<CommandChoice, kTransformSpaceItems.size()> kSpaceChoices = [] {
    std::array<CommandChoice, kTransformSpaceItems.size()> choices{};
    for (std::size_t i = 0; i < kTransformSpaceItems.size(); ++i)
        choices[i] = kTransformSpaceItems[i].Choice;
    return choices;
}();
} // namespace

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

std::span<const IRadialMenuModel::MenuItem> TransformModeMenuModel::Variants(int index) const
{
    if (index < 0 || index >= Count())
        return {};
    return kSpaceChoices;
}

int TransformModeMenuModel::ActiveVariant(int index) const
{
    const ManipulatorSession* session = SessionResolver ? SessionResolver() : nullptr;
    if (session == nullptr || index != ActiveIndex())
        return -1;
    for (std::size_t i = 0; i < kTransformSpaceItems.size(); ++i)
        if (kTransformSpaceItems[i].Space == session->GetTransformSpace())
            return static_cast<int>(i);
    return -1;
}

void TransformModeMenuModel::Select(int index, int variant)
{
    ManipulatorSession* session = SessionResolver ? SessionResolver() : nullptr;
    if (session == nullptr || index < 0 || index >= Count())
        return;
    session->SetTransformMode(kTransformModeItems[static_cast<std::size_t>(index)].Mode);
    if (variant >= 0 && variant < static_cast<int>(kTransformSpaceItems.size()))
        session->SetTransformSpace(kTransformSpaceItems[static_cast<std::size_t>(variant)].Space);
}
