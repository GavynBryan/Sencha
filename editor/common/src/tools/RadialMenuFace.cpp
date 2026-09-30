#include "tools/RadialMenuFace.h"

#include "tools/RadialMenuModel.h"
#include "tools/RadialMenuSession.h"

RadialMenuFace FaceOf(const RadialMenuSession& wheel, const IRadialMenuModel& menu)
{
    RadialMenuFace face;
    if (wheel.GetPhase() != RadialMenuPhase::Open)
        return face;

    const RadialMenu::Layout& layout = wheel.GetLayout();
    const int hot = wheel.GetHot();
    const int hotVariant = wheel.GetHotVariant();
    const int active = menu.ActiveIndex();
    const int count = menu.Count();
    const float radius = layout.Button * 0.5f;

    face.Open = true;
    face.Centre = layout.Center;
    face.PlateRadius = layout.RimOuter();
    face.HubRadius = layout.Hub;
    face.CaptionTop = layout.CaptionY();
    face.CaptionHeight = layout.CaptionHeight;
    face.CaptionNamesCurrent = hot < 0;

    for (int index = 0; index < count; ++index)
    {
        const IRadialMenuModel::MenuItem item = menu.Item(index);
        face.Keys.push_back({ .Centre = RadialMenu::SlotCenter(layout, index),
                              .Radius = radius,
                              .Label = std::string(item.Label),
                              .Icon = item.Icon,
                              .Hot = index == hot,
                              .Active = index == active });
        if (index == hot || (hot < 0 && index == active))
            face.Caption = item.Label;
    }
    if (hot >= 0 && hot < count)
    {
        const std::span<const IRadialMenuModel::MenuItem> choices = menu.Variants(hot);
        const int choiceCount = static_cast<int>(choices.size());
        const int current = menu.ActiveVariant(hot);
        for (int v = 0; v < choiceCount; ++v)
        {
            const IRadialMenuModel::MenuItem& choice = choices[static_cast<std::size_t>(v)];
            face.Keys.push_back({ .Centre = RadialMenu::VariantSlotCenter(layout, hot, v, choiceCount),
                                  .Radius = radius,
                                  .Label = std::string(choice.Label),
                                  .Icon = choice.Icon,
                                  .Hot = v == hotVariant,
                                  .Active = v == current,
                                  .Variant = true });
        }
        if (hotVariant >= 0 && hotVariant < choiceCount)
            face.Caption += std::string(" \xC2\xB7 ") + std::string(choices[static_cast<std::size_t>(hotVariant)].Label);
    }
    return face;
}
