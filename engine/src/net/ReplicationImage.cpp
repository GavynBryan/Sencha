#include <net/ReplicationImage.h>

#include <net/ReplicationLayout.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace
{
    constexpr std::size_t kCountWordBytes = 8;

    constexpr std::size_t AlignUp(std::size_t bytes)
    {
        return (bytes + 7) & ~std::size_t{ 7 };
    }

    std::uint32_t CountAt(std::span<const std::byte> image, std::size_t at)
    {
        std::uint32_t count = 0;
        std::memcpy(&count, image.data() + at, sizeof(count));
        return count;
    }

    void StoreCount(std::span<std::byte> image, std::size_t at, std::size_t count)
    {
        const auto value = static_cast<std::uint32_t>(count);
        std::memcpy(image.data() + at, &value, sizeof(value));
        std::memset(image.data() + at + sizeof(value), 0, kCountWordBytes - sizeof(value));
    }
}

std::size_t ReplicationImageHeadBytes(const ReplicatedComponent& layout)
{
    return layout.Lists.empty() ? layout.Size : AlignUp(layout.Size);
}

bool ReplicationSplitImage(const ReplicatedComponent& layout, std::span<const std::byte> image,
                           ReplicationImageParts& parts)
{
    std::size_t at = ReplicationImageHeadBytes(layout);
    for (std::size_t list = 0; list < layout.Lists.size(); ++list)
    {
        if (image.size() < at + kCountWordBytes)
            return false;
        const std::uint32_t count = CountAt(image, at);
        if (count > layout.Lists[list].MaxCount)
            return false;
        at += kCountWordBytes;
        parts.Lists[list] = ReplicationImageList{ .Offset = at, .Count = count };
        at += AlignUp(count * layout.Lists[list].Stride);
    }
    return image.size() == at;
}

ReplicationImage::ReplicationImage(const ReplicatedComponent& layout) : Layout_(&layout)
{
    Reset();
}

void ReplicationImage::Reset(const ReplicatedComponent& layout)
{
    Layout_ = &layout;
    Reset();
}

void ReplicationImage::Reset()
{
    const std::size_t head = ReplicationImageHeadBytes(*Layout_);
    Bytes_.assign(head + kCountWordBytes * Layout_->Lists.size(), std::byte{ 0 });
    for (std::size_t list = 0; list < Layout_->Lists.size(); ++list)
        Parts_.Lists[list] = ReplicationImageList{ .Offset = head + kCountWordBytes * (list + 1), .Count = 0 };
    Clamped_ = 0;
}

bool ReplicationImage::Assign(std::span<const std::byte> bytes)
{
    ReplicationImageParts parts;
    if (!ReplicationSplitImage(*Layout_, bytes, parts))
        return false;
    Bytes_.assign(bytes.begin(), bytes.end());
    Parts_ = parts;
    return true;
}

std::span<std::byte> ReplicationImage::Head()
{
    return std::span(Bytes_).subspan(0, Layout_->Size);
}

std::size_t ReplicationImage::Count(std::size_t list) const
{
    assert(list < Layout_->Lists.size());
    return Parts_.Lists[list].Count;
}

std::span<const std::byte> ReplicationImage::Elements(std::size_t list) const
{
    assert(list < Layout_->Lists.size());
    const ReplicationImageList& part = Parts_.Lists[list];
    return std::span(Bytes_).subspan(part.Offset, part.Count * Layout_->Lists[list].Stride);
}

std::span<std::byte> ReplicationImage::MutableElements(std::size_t list)
{
    assert(list < Layout_->Lists.size());
    const ReplicationImageList& part = Parts_.Lists[list];
    return std::span(Bytes_).subspan(part.Offset, part.Count * Layout_->Lists[list].Stride);
}

std::span<const std::byte> ReplicationImage::Elements(std::size_t list, std::size_t stride) const
{
    assert(list < Layout_->Lists.size() && stride == Layout_->Lists[list].Stride);
    (void)stride;
    return Elements(list);
}

std::span<std::byte> ReplicationImage::ResizeElements(std::size_t list, std::size_t count, std::size_t stride)
{
    assert(list < Layout_->Lists.size() && stride == Layout_->Lists[list].Stride);
    (void)stride;
    return ResizeElements(list, count);
}

std::span<std::byte> ReplicationImage::ResizeElements(std::size_t list, std::size_t count)
{
    assert(list < Layout_->Lists.size());
    const ReplicatedList& declared = Layout_->Lists[list];
    if (count > declared.MaxCount)
    {
        Clamped_ += count - declared.MaxCount;
        count = declared.MaxCount;
    }

    ReplicationImageList& part = Parts_.Lists[list];
    const std::size_t stride = declared.Stride;
    const std::size_t had = part.Count;
    const std::size_t oldSpan = AlignUp(had * stride);
    const std::size_t newSpan = AlignUp(count * stride);
    const std::size_t tail = part.Offset + oldSpan;
    const std::size_t after = Bytes_.size() - tail;

    if (newSpan > oldSpan)
    {
        Bytes_.resize(Bytes_.size() + newSpan - oldSpan);
        std::memmove(Bytes_.data() + part.Offset + newSpan, Bytes_.data() + tail, after);
    }
    else if (newSpan < oldSpan)
    {
        std::memmove(Bytes_.data() + part.Offset + newSpan, Bytes_.data() + tail, after);
        Bytes_.resize(Bytes_.size() - (oldSpan - newSpan));
    }
    for (std::size_t later = list + 1; later < Layout_->Lists.size(); ++later)
        Parts_.Lists[later].Offset = Parts_.Lists[later].Offset + newSpan - oldSpan;

    for (std::size_t element = had; element < count; ++element)
        std::memcpy(Bytes_.data() + part.Offset + element * stride, declared.ElementDefault.data(), stride);
    std::memset(Bytes_.data() + part.Offset + count * stride, 0, newSpan - count * stride);

    part.Count = count;
    StoreCount(Bytes_, part.Offset - kCountWordBytes, count);
    return std::span(Bytes_).subspan(part.Offset, count * stride);
}
