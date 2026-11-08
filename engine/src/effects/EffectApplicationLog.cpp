#include <effects/EffectApplicationLog.h>

#include <attributes/AttributeSet.h>

void EffectApplicationLog::Begin(EntityId target, EffectId effect, const EffectSource& source, float scale)
{
    Rows.push_back({ .Target = target,
                     .Effect = effect,
                     .Source = source,
                     .Scale = scale,
                     .FirstChange = static_cast<std::uint32_t>(Changes.size()),
                     .ChangeCount = 0 });
}

void EffectApplicationLog::Note(AttributeId attribute, float before)
{
    if (Rows.empty())
        return;
    for (const EffectAttributeChange& change : ChangesOf(Rows.back()))
        if (change.Attribute == attribute)
            return;
    Changes.push_back({ attribute, before, before });
    ++Rows.back().ChangeCount;
}

void EffectApplicationLog::Settle(const AttributeSet& set)
{
    if (Rows.empty())
        return;
    const EffectApplication& row = Rows.back();
    for (std::uint32_t i = row.FirstChange; i < row.FirstChange + row.ChangeCount; ++i)
        Changes[i].After = set.GetBase(Changes[i].Attribute, Changes[i].Before);
}

void EffectApplicationLog::Clear()
{
    Rows.clear();
    Changes.clear();
}

std::span<const EffectAttributeChange> EffectApplicationLog::ChangesOf(const EffectApplication& application) const
{
    return std::span(Changes).subspan(application.FirstChange, application.ChangeCount);
}

float EffectApplicationLog::ChangeIn(const EffectApplication& application, AttributeId attribute) const
{
    for (const EffectAttributeChange& change : ChangesOf(application))
        if (change.Attribute == attribute)
            return change.After - change.Before;
    return 0.0f;
}
