// The incremental digest behind shadow-view invalidation and the editor's
// cook-staleness badge.
//
// Four copies of this loop existed before it had a home. It is pinned against
// published FNV-1a vectors rather than against itself, because "whatever the
// current implementation produces" would have accepted all four copies equally,
// including a drifted one.

#include <gtest/gtest.h>

#include <core/hash/Fnv1a.h>

#include <cstring>
#include <string_view>

namespace
{

std::uint64_t HashText(std::string_view text)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aBytes(hash, text.data(), text.size());
    return hash;
}

} // namespace

TEST(Fnv1a, MatchesThePublishedVectors)
{
    // FNV-1a 64-bit reference values.
    EXPECT_EQ(HashText(""), 0xcbf29ce484222325ull);
    EXPECT_EQ(HashText("a"), 0xaf63dc4c8601ec8cull);
    EXPECT_EQ(HashText("foobar"), 0x85944171f73967e8ull);
}

TEST(Fnv1a, TheOffsetBasisIsTheEmptyHash)
{
    EXPECT_EQ(kFnv1aOffsetBasis, 0xcbf29ce484222325ull);
}

TEST(Fnv1a, IsOrderSensitive)
{
    // The digests this backs summarise a walk, so two different walks over the
    // same values must not collide.
    std::uint64_t forward = kFnv1aOffsetBasis;
    HashFnv1aByte(forward, 1);
    HashFnv1aByte(forward, 2);

    std::uint64_t backward = kFnv1aOffsetBasis;
    HashFnv1aByte(backward, 2);
    HashFnv1aByte(backward, 1);

    EXPECT_NE(forward, backward);
}

TEST(Fnv1a, AccumulatesIncrementallyLikeOneCall)
{
    std::uint64_t split = kFnv1aOffsetBasis;
    HashFnv1aBytes(split, "foo", 3);
    HashFnv1aBytes(split, "bar", 3);

    EXPECT_EQ(split, HashText("foobar"))
        << "callers fold field by field and compare against whole-run digests";
}

TEST(Fnv1a, HashingAValueMatchesHashingItsBytes)
{
    struct Payload
    {
        float A;
        std::uint32_t B;
    };
    const Payload payload{ 1.5f, 7 };

    std::uint64_t viaValue = kFnv1aOffsetBasis;
    HashFnv1aValue(viaValue, payload);

    std::uint64_t viaBytes = kFnv1aOffsetBasis;
    HashFnv1aBytes(viaBytes, &payload, sizeof(payload));

    EXPECT_EQ(viaValue, viaBytes);
}

TEST(Fnv1a, DistinguishesValuesThatDifferInOneField)
{
    struct Payload
    {
        float A;
        std::uint32_t B;
    };

    std::uint64_t first = kFnv1aOffsetBasis;
    HashFnv1aValue(first, Payload{ 1.5f, 7 });

    std::uint64_t second = kFnv1aOffsetBasis;
    HashFnv1aValue(second, Payload{ 1.5f, 8 });

    EXPECT_NE(first, second);
}

TEST(Fnv1a, ZeroLengthFoldsNothing)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aBytes(hash, "", 0);
    EXPECT_EQ(hash, kFnv1aOffsetBasis);
}
