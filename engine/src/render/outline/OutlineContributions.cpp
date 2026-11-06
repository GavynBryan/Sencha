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

bool IsOutlined(const World& world, EntityId entity)
{
    const OutlineContributions* contributions = world.TryGetResource<OutlineContributions>();
    return contributions != nullptr && contributions->ByEntity.Find(entity) != nullptr;
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

void OutlineFrame::Finish(const Mat4& viewProjection)
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
    UpdateScreenBounds(viewProjection);
}

void OutlineFrame::UpdateScreenBounds(const Mat4& viewProjection)
{
    ScreenMin[0] = ScreenMin[1] = 1.0f;
    ScreenMax[0] = ScreenMax[1] = -1.0f;
    bool behindCamera = false;
    const auto cover = [&](const RenderQueueItem& item) {
        const Vec3d low = item.WorldBounds.Min;
        const Vec3d high = item.WorldBounds.Max;
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vec4 clip = viewProjection * Vec4((corner & 1) != 0 ? high.X : low.X, (corner & 2) != 0 ? high.Y : low.Y,
                                                    (corner & 4) != 0 ? high.Z : low.Z, 1.0f);
            if (clip.W <= 0.0001f)
            {
                behindCamera = true;
                return;
            }
            ScreenMin[0] = std::min(ScreenMin[0], clip.X / clip.W);
            ScreenMin[1] = std::min(ScreenMin[1], clip.Y / clip.W);
            ScreenMax[0] = std::max(ScreenMax[0], clip.X / clip.W);
            ScreenMax[1] = std::max(ScreenMax[1], clip.Y / clip.W);
        }
    };
    for (const OutlineStyleId style : DrawOrder)
    {
        for (const RenderQueueItem& item : Queues[style.Slot].Opaque())
            cover(item);
        for (const RenderQueueItem& item : Queues[style.Slot].Transparent())
            cover(item);
    }
    if (behindCamera)
    {
        ScreenMin[0] = ScreenMin[1] = -1.0f;
        ScreenMax[0] = ScreenMax[1] = 1.0f;
    }
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
