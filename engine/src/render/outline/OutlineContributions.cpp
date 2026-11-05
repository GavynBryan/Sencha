#include <render/outline/OutlineContributions.h>

#include <ecs/World.h>

#include <algorithm>
#include <cmath>

namespace
{
    struct OutlineStyleRecord
    {
        std::string Name;
        OutlineStyleEntry Entry;
    };

    // World resources: presentation-only, never replicated, saved or read by a
    // fixed-tick system.
    struct OutlineStyles
    {
        std::vector<OutlineStyleRecord> Records;
        std::uint32_t Version = 0;
    };

    struct OutlineContributions
    {
        PresentationContributions<EntityId, OutlineContribution, EntityIdHash> ByEntity;
    };

    float SrgbToLinear(float encoded)
    {
        return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }
}

OutlineStyleId RegisterOutlineStyle(World& world, std::string_view name, const OutlineStyleDesc& desc)
{
    if (!world.HasResource<OutlineStyles>())
        world.AddResource<OutlineStyles>();
    OutlineStyles& styles = world.GetResource<OutlineStyles>();
    const bool priorityTaken = std::ranges::any_of(
        styles.Records, [&](const OutlineStyleRecord& record) { return record.Entry.Priority == desc.Priority; });
    if (priorityTaken || styles.Records.size() >= kMaxOutlineStyles)
        return {};

    styles.Records.push_back({ .Name = std::string(name),
                               .Entry = { .LinearColor = Vec<3>(SrgbToLinear(desc.Color.X), SrgbToLinear(desc.Color.Y),
                                                                SrgbToLinear(desc.Color.Z)),
                                          .WidthPixels = desc.WidthPixels,
                                          .Priority = desc.Priority } });
    ++styles.Version;
    return OutlineStyleId{ static_cast<std::uint8_t>(styles.Records.size()) };
}

void SetOutline(World& world, EntityId entity, const OutlineContribution& contribution)
{
    if (!world.HasResource<OutlineContributions>())
        world.AddResource<OutlineContributions>();
    (void)world.GetResource<OutlineContributions>().ByEntity.Set(entity, contribution);
}

void ClearOutline(World& world, EntityId entity, const PresentationOwner& owner)
{
    if (OutlineContributions* contributions = world.TryGetResource<OutlineContributions>())
        (void)contributions->ByEntity.Clear(entity, owner);
}

void ClearOutlinesOf(World& world, PresentationWriterId writer)
{
    if (OutlineContributions* contributions = world.TryGetResource<OutlineContributions>())
        contributions->ByEntity.ClearWriter(writer, [](EntityId, const OutlineContribution&) {});
}

RenderQueue* OutlineFrame::QueueFor(EntityId entity)
{
    const auto found = StyleOf.find(entity);
    return found != StyleOf.end() ? &Queues[found->second.Slot] : nullptr;
}

void OutlineFrame::Reset()
{
    StyleOf.clear();
    DrawOrder.clear();
    MaxWidthPixels = 0.0f;
    for (RenderQueue& queue : Queues)
        if (!queue.Opaque().empty() || !queue.Transparent().empty())
            queue.Reset();
}

void OutlineFrame::Finish()
{
    DrawOrder.clear();
    for (std::uint8_t slot = 1; slot <= kMaxOutlineStyles; ++slot)
    {
        RenderQueue& queue = Queues[slot];
        if (queue.Opaque().empty() && queue.Transparent().empty())
            continue;
        queue.SortOpaque();
        DrawOrder.push_back(OutlineStyleId{ slot });
    }
    std::ranges::sort(DrawOrder, [this](OutlineStyleId a, OutlineStyleId b) {
        return Styles[a.Slot].Priority < Styles[b.Slot].Priority;
    });
}

void ExtractOutlines(World& world, OutlineFrame& out)
{
    out.Reset();
    OutlineContributions* contributions = world.TryGetResource<OutlineContributions>();
    if (contributions == nullptr || contributions->ByEntity.KeyCount() == 0)
        return;

    contributions->ByEntity.RemoveWhere(
        [&](EntityId entity, const OutlineContribution& contribution) {
            const LeaseOwner& holder = contribution.Owner.Holder;
            return !world.IsAlive(entity) || (holder.IsSet() && !world.IsAlive(holder.Entity));
        },
        [](EntityId, const OutlineContribution&) {});

    const OutlineStyles* styles = world.TryGetResource<OutlineStyles>();
    if (styles == nullptr)
        return;
    out.StylesVersion = styles->Version;
    for (std::size_t i = 0; i < styles->Records.size(); ++i)
        out.Styles[i + 1] = styles->Records[i].Entry;

    contributions->ByEntity.ForEachKey([&](EntityId entity, const std::vector<OutlineContribution>&) {
        const OutlineContribution* winner = contributions->ByEntity.Resolve(entity);
        if (winner == nullptr || !winner->Style.IsValid() || winner->Style.Slot > styles->Records.size())
            return;
        out.StyleOf.emplace(entity, winner->Style);
        out.MaxWidthPixels = std::max(out.MaxWidthPixels, out.Styles[winner->Style.Slot].WidthPixels);
    });
}
