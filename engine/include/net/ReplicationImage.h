#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

struct ReplicatedComponent;

inline constexpr std::size_t kMaxReplicatedListsPerComponent = 4;

// Where each list of a well-formed image sits: its first element and its count.
struct ReplicationImageList
{
    std::size_t Offset = 0;
    std::size_t Count = 0;
};

struct ReplicationImageParts
{
    std::array<ReplicationImageList, kMaxReplicatedListsPerComponent> Lists{};
};

// Bytes before the first list: the head, padded to eight when lists follow.
[[nodiscard]] std::size_t ReplicationImageHeadBytes(const ReplicatedComponent& layout);
[[nodiscard]] bool ReplicationSplitImage(const ReplicatedComponent& layout, std::span<const std::byte> image,
                                         ReplicationImageParts& parts);

// A component's wire image: its head, then each list as a count and its elements.
class ReplicationImage
{
public:
    explicit ReplicationImage(const ReplicatedComponent& layout);

    void Reset();
    void Reset(const ReplicatedComponent& layout);
    [[nodiscard]] bool Assign(std::span<const std::byte> bytes);

    template <typename Head>
    [[nodiscard]] Head& HeadAs()
    {
        static_assert(std::is_trivially_copyable_v<Head>);
        return *reinterpret_cast<Head*>(Bytes_.data());
    }

    template <typename Head>
    [[nodiscard]] const Head& HeadAs() const
    {
        static_assert(std::is_trivially_copyable_v<Head>);
        return *reinterpret_cast<const Head*>(Bytes_.data());
    }

    // Asking past the list's most keeps the most and counts the rest in Clamped.
    template <typename Element>
    [[nodiscard]] std::span<Element> Resize(std::size_t list, std::size_t count)
    {
        static_assert(std::is_trivially_copyable_v<Element> && alignof(Element) <= 8);
        const std::span<std::byte> bytes = ResizeElements(list, count, sizeof(Element));
        return { reinterpret_cast<Element*>(bytes.data()), bytes.size() / sizeof(Element) };
    }

    template <typename Element>
    [[nodiscard]] std::span<const Element> List(std::size_t list) const
    {
        static_assert(std::is_trivially_copyable_v<Element> && alignof(Element) <= 8);
        const std::span<const std::byte> bytes = Elements(list, sizeof(Element));
        return { reinterpret_cast<const Element*>(bytes.data()), bytes.size() / sizeof(Element) };
    }

    [[nodiscard]] std::span<std::byte> ResizeElements(std::size_t list, std::size_t count);
    [[nodiscard]] std::span<std::byte> MutableElements(std::size_t list);
    [[nodiscard]] std::span<const std::byte> Elements(std::size_t list) const;
    [[nodiscard]] std::span<std::byte> Head();

    [[nodiscard]] std::size_t Count(std::size_t list) const;
    [[nodiscard]] std::size_t Clamped() const { return Clamped_; }
    [[nodiscard]] std::span<const std::byte> Bytes() const { return Bytes_; }
    [[nodiscard]] const ReplicatedComponent& Layout() const { return *Layout_; }

private:
    [[nodiscard]] std::span<std::byte> ResizeElements(std::size_t list, std::size_t count, std::size_t stride);
    [[nodiscard]] std::span<const std::byte> Elements(std::size_t list, std::size_t stride) const;

    const ReplicatedComponent* Layout_ = nullptr;
    std::vector<std::byte> Bytes_;
    ReplicationImageParts Parts_;
    std::size_t Clamped_ = 0;
};
