#include "tools/RadialMenuFace.h"

#include "tools/RadialMenuModel.h"
#include "tools/RadialMenuSession.h"

#include <numbers>

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTurn = 2.0f * kPi;

// A petal from `inner` to `outer` over `span`, its flanks drawn in by `seam`
// pixels at its middle radius so neighbours stand apart.
RadialMenuSector Petal(float inner, float outer, RadialMenu::Span span, float seam, bool hot)
{
    const float inset = seam / ((inner + outer) * 0.5f);
    return { inner, outer, span.Begin + inset, span.End - span.Begin - inset * 2.0f,
             hot ? RadialMenuPart::HotPetal : RadialMenuPart::Petal };
}

RadialMenuSector Band(float inner, float outer, RadialMenu::Span span, RadialMenuPart part)
{
    return { inner, outer, span.Begin, span.End - span.Begin, part };
}

RadialMenuSector FullTurn(float inner, float outer, RadialMenuPart part)
{
    return { inner, outer, 0.0f, kTurn, part };
}
} // namespace

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
    const float well = layout.Button * 0.42f;
    const float seam = layout.Seam;
    const float rim = layout.Rim;
    const float outer = layout.RimOuter();
    const float petalInner = layout.Hub + seam;
    const float petalOuter = layout.Radius + layout.Button * 0.5f;

    face.Open = true;
    face.Centre = layout.Center;
    face.PlateRadius = outer;
    face.CaptionTop = layout.CaptionY();
    face.CaptionHeight = layout.CaptionHeight;
    face.CaptionNamesCurrent = hot < 0;
    std::vector<RadialMenuSector>& sectors = face.Sectors;

    for (int index = 0; index < count; ++index)
    {
        const IRadialMenuModel::MenuItem item = menu.Item(index);
        face.Keys.push_back({ .Centre = RadialMenu::SlotCenter(layout, index),
                              .Radius = well,
                              .Label = std::string(item.Label),
                              .Icon = item.Icon,
                              .Hot = index == hot,
                              .Active = index == active });
        if (index == hot || (hot < 0 && index == active))
            face.Caption = item.Label;
    }

    // The hot entry's variants: a fan of the same petals grown out of the plate
    // beyond the rim, closed by an arc of the same rim.
    const std::span<const IRadialMenuModel::MenuItem> choices =
        hot >= 0 && hot < count ? menu.Variants(hot) : std::span<const IRadialMenuModel::MenuItem>{};
    const int choiceCount = static_cast<int>(choices.size());
    if (choiceCount > 0)
    {
        const int current = menu.ActiveVariant(hot);
        const float fanInner = outer + seam;
        const float fanOuter = layout.OuterRadius + layout.Button * 0.5f;
        const float fanExtent = fanOuter + seam + rim;
        const RadialMenu::Range fan = RadialMenu::FanRange(layout, hot, choiceCount);
        const float margin = rim / fanExtent;
        // The fan's plate runs in under the rim, so the two never meet edge on edge.
        const RadialMenu::Span arc{ fan.Begin - margin, fan.Begin + fan.Width + margin };
        sectors.push_back(Band(outer - rim, fanExtent - rim, arc, RadialMenuPart::Plate));
        sectors.push_back(Band(fanExtent - rim, fanExtent, arc, RadialMenuPart::Rim));
        if (current >= 0 && current < choiceCount)
            sectors.push_back(Band(fanExtent - rim * 0.7f, fanExtent - rim * 0.3f,
                                   RadialMenu::VariantSpan(layout, hot, current, choiceCount), RadialMenuPart::Mark));
        for (int v = 0; v < choiceCount; ++v)
        {
            const IRadialMenuModel::MenuItem& choice = choices[static_cast<std::size_t>(v)];
            sectors.push_back(Petal(fanInner, fanOuter, RadialMenu::VariantSpan(layout, hot, v, choiceCount), seam,
                                    v == hotVariant));
            face.Keys.push_back({ .Centre = RadialMenu::VariantSlotCenter(layout, hot, v, choiceCount),
                                  .Radius = well,
                                  .Label = std::string(choice.Label),
                                  .Icon = choice.Icon,
                                  .Hot = v == hotVariant,
                                  .Active = v == current,
                                  .Variant = true });
        }
        if (hotVariant >= 0 && hotVariant < choiceCount)
            face.Caption += std::string(" \xC2\xB7 ") + std::string(choices[static_cast<std::size_t>(hotVariant)].Label);
    }

    // The wheel: plate, the rim with its marks, the petals, the hub.
    sectors.push_back(FullTurn(0.0f, outer, RadialMenuPart::Plate));
    sectors.push_back(FullTurn(outer - rim, outer, RadialMenuPart::Rim));
    sectors.push_back(Band(outer - rim * 0.65f, outer - rim * 0.35f, { kPi * 1.55f, kPi * 1.95f }, RadialMenuPart::Detail));
    sectors.push_back(Band(outer - rim * 0.65f, outer - rim * 0.35f, { kPi * 0.65f, kPi * 0.85f }, RadialMenuPart::Detail));
    if (active >= 0 && active < count)
        sectors.push_back(Band(outer - rim * 0.7f, outer - rim * 0.3f, RadialMenu::SectorSpan(active, layout.Count),
                               RadialMenuPart::Mark));
    for (int index = 0; index < count; ++index)
        sectors.push_back(Petal(petalInner, petalOuter, RadialMenu::SectorSpan(index, layout.Count), seam, index == hot));
    sectors.push_back(FullTurn(0.0f, layout.Hub, RadialMenuPart::Hub));
    return face;
}
