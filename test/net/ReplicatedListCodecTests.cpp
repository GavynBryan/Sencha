#include <gtest/gtest.h>

#include "ReplicatedListFixture.h"

#include <net/ReplicationCodec.h>
#include <net/ReplicationImage.h>
#include <net/ReplicationLayout.h>
#include <net/ReplicationRuntime.h>

#include <array>
#include <cstring>
#include <span>
#include <vector>

namespace
{
    constexpr std::size_t kScratchBytes = 8192;

    struct RosterLayout
    {
        ReplicationLayout Layout;
        const ReplicatedComponent* Component = nullptr;

        RosterLayout()
        {
            EXPECT_TRUE(Layout.AddCodec<Roster>());
            EXPECT_TRUE(Layout.AddCodec<Ledger>());
            Layout.Seal();
            Component = Layout.Find(ResolveComponentTypeId<Roster>());
        }

        [[nodiscard]] const ReplicatedComponent& Ledgers() const
        {
            return *Layout.Find(ResolveComponentTypeId<Ledger>());
        }
    };

    std::uint64_t ListBit(const ReplicatedComponent& component, std::size_t list)
    {
        return std::uint64_t{ 1 } << (component.Fields.size() + list);
    }

    Roster RosterOf(std::uint32_t label, std::initializer_list<std::uint32_t> values)
    {
        Roster roster;
        roster.Label = label;
        for (const std::uint32_t value : values)
        {
            roster.Entries[roster.Count] = RosterEntry{ .Value = value, .Secret = value * 10, .Target = {} };
            ++roster.Count;
        }
        return roster;
    }

    void ImageOf(const ReplicatedComponent& component, const Roster& roster, ReplicationImage& image)
    {
        image.Reset();
        component.ToWire(ReplicationWireContext{}, std::span(reinterpret_cast<const std::byte*>(&roster), sizeof(roster)),
                         image);
    }

    Roster RosterFrom(const ReplicatedComponent& component, const ReplicationImage& image)
    {
        Roster roster;
        component.FromWire(ReplicationWireContext{}, image, std::span(reinterpret_cast<std::byte*>(&roster), sizeof(roster)));
        return roster;
    }

    std::vector<std::byte> Encode(const ReplicatedComponent& component, const ReplicationImage& image,
                                  std::uint64_t fields, std::span<const std::uint64_t> elements)
    {
        std::vector<std::byte> scratch(kScratchBytes);
        NetBitWriter writer(scratch);
        EXPECT_TRUE(ReplicationEncodeComponent(component, image.Bytes(), fields, writer, elements));
        EXPECT_EQ(writer.BitsWritten(), ReplicationEncodedComponentBits(component, image.Bytes(), fields, elements));
        scratch.resize(writer.BytesWritten());
        return scratch;
    }

    std::vector<std::byte> EncodeAll(const ReplicatedComponent& component, const ReplicationImage& image)
    {
        const std::uint64_t fields = ReplicationVisibleFields(component, true);
        return Encode(component, image, fields, EveryElementRun(component, image, fields));
    }
}

TEST(ReplicationLayout, AListTooLargeIsRefused)
{
    ReplicationLayout layout;
    EXPECT_FALSE(layout.AddCodec<Huge>());
    EXPECT_EQ(layout.Error(), ReplicationLayoutError::ListTooLarge);
    EXPECT_EQ(layout.ErrorDetail(), "test.Huge");
}

TEST(ReplicationLayout, AListIsCompiledFromItsElementsSchema)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ASSERT_EQ(component.Lists.size(), 1u);
    const ReplicatedList& list = component.Lists[0];
    EXPECT_EQ(list.Name, "entries");
    EXPECT_EQ(list.MaxCount, kRosterMost);
    EXPECT_EQ(list.Stride, sizeof(RosterEntryWire));
    ASSERT_EQ(list.Fields.size(), 3u);
    EXPECT_FALSE(list.Fields[0].OwnerOnly);
    EXPECT_TRUE(list.Fields[1].OwnerOnly);
    EXPECT_EQ(component.Size, sizeof(RosterHeadWire));
    EXPECT_TRUE(roster.Ledgers().Fields.empty()) << "a component may be its lists alone";
}

TEST(ReplicationLayoutHash, AListIsPartOfTheContract)
{
    ReplicationLayout withList;
    ASSERT_TRUE(withList.AddCodec<Roster>());
    ReplicationLayout other;
    ASSERT_TRUE(other.AddCodec<Ledger>());
    EXPECT_NE(withList.TableHash(), other.TableHash());
}

TEST(ReplicationCodec, AListRoundTripsItsCountAndEveryElement)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ReplicationImage sent(component);
    ImageOf(component, RosterOf(7, { 3, 1, 4, 1, 5 }), sent);
    ASSERT_EQ(sent.Count(0), 5u);

    const std::vector<std::byte> bytes = EncodeAll(component, sent);
    ReplicationImage received(component);
    NetBitReader reader(bytes);
    ASSERT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::None);

    const Roster arrived = RosterFrom(component, received);
    EXPECT_EQ(arrived.Label, 7u);
    ASSERT_EQ(arrived.Count, 5u);
    EXPECT_EQ(arrived.Entries[2].Value, 4u);
    EXPECT_EQ(arrived.Entries[4].Secret, 50u);
    EXPECT_TRUE(std::equal(sent.Bytes().begin(), sent.Bytes().end(), received.Bytes().begin(), received.Bytes().end()));
}

TEST(ReplicationCodec, AListAtItsMostDecodesAndOnePastIsRefused)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    Roster full;
    full.Count = kRosterMost;
    ReplicationImage sent(component);
    ImageOf(component, full, sent);
    ASSERT_EQ(sent.Count(0), kRosterMost);
    {
        const std::vector<std::byte> bytes = EncodeAll(component, sent);
        ReplicationImage received(component);
        NetBitReader reader(bytes);
        EXPECT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::None);
        EXPECT_EQ(received.Count(0), kRosterMost);
    }

    std::array<std::byte, 64> scratch{};
    NetBitWriter writer(scratch);
    writer.WriteBool(false);
    writer.WriteBool(true);
    writer.WriteVarUInt(kRosterMost + 1);
    for (std::size_t i = 0; i < kRosterMost + 1; ++i)
        writer.WriteBool(false);
    ReplicationImage received(component);
    NetBitReader reader(writer.Written());
    EXPECT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::CountAboveMost);
}

TEST(ReplicationCodec, AnEmptiedListTravelsAsACountOfZero)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ReplicationImage received(component);
    ImageOf(component, RosterOf(1, { 4, 5 }), received);
    ReplicationImage emptied(component);
    ImageOf(component, RosterOf(1, {}), emptied);

    std::vector<std::byte> scratch(kScratchBytes);
    NetBitWriter writer(scratch);
    ASSERT_TRUE(ReplicationEncodeComponent(component, emptied.Bytes(), ListBit(component, 0), writer, {}));
    EXPECT_EQ(writer.BitsWritten(), ReplicationEncodedComponentBits(component, emptied.Bytes(), ListBit(component, 0)));
    NetBitReader reader(writer.Written());
    ASSERT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::None);
    EXPECT_EQ(received.Count(0), 0u);
}

TEST(ReplicationCodec, ATruncatedListIsRefused)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ReplicationImage sent(component);
    ImageOf(component, RosterOf(9, { 11, 12, 13 }), sent);
    const std::vector<std::byte> bytes = EncodeAll(component, sent);
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        ReplicationImage received(component);
        NetBitReader reader(std::span<const std::byte>(bytes).subspan(0, length));
        EXPECT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::Truncated)
            << "accepted a list truncated to " << length << " bytes";
    }
}

TEST(ReplicationCodec, AListShrinksThenRegrowsFromItsElementDefault)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ReplicationImage received(component);
    ImageOf(component, RosterOf(1, { 21, 22, 23, 24, 25 }), received);

    ReplicationImage shrunk(component);
    ImageOf(component, RosterOf(1, { 21, 22 }), shrunk);
    {
        const std::vector<std::uint64_t> none(2, 0);
        const std::vector<std::byte> bytes = Encode(component, shrunk, ListBit(component, 0), none);
        NetBitReader reader(bytes);
        ASSERT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::None);
    }
    EXPECT_EQ(received.Count(0), 2u);
    EXPECT_EQ(received.List<RosterEntryWire>(0)[1].Value, 22u) << "an element the message left alone is kept";

    ReplicationImage regrown(component);
    ImageOf(component, RosterOf(1, { 21, 22, 0, 99 }), regrown);
    {
        const std::vector<std::uint64_t> onlyLast{ 0, 0, 0, 0b111 };
        const std::vector<std::byte> bytes = Encode(component, regrown, ListBit(component, 0), onlyLast);
        NetBitReader reader(bytes);
        ASSERT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::None);
    }
    ASSERT_EQ(received.Count(0), 4u);
    const std::span<const RosterEntryWire> held = received.List<RosterEntryWire>(0);
    EXPECT_EQ(held[2].Value, 0u) << "a new element starts at the element default, not what stood there before";
    EXPECT_EQ(held[2].Secret, 0u);
    EXPECT_EQ(held[3].Value, 99u);
    EXPECT_EQ(held[3].Secret, 990u);
}

TEST(ReplicationCodec, AListsRunsKeepTheirAudience)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    EXPECT_NE(ReplicationVisibleFields(component, false) & ListBit(component, 0), 0u)
        << "a list with one public run is visible to everyone";
    EXPECT_EQ(ReplicationVisibleElementRuns(component.Lists[0], false), 0b101u);
    EXPECT_EQ(ReplicationVisibleElementRuns(component.Lists[0], true), 0b111u);

    const ReplicatedComponent& ledgers = roster.Ledgers();
    EXPECT_EQ(ReplicationVisibleFields(ledgers, false), 0u) << "an all-owner-only list costs a non-owner nothing";
    EXPECT_EQ(ReplicationVisibleFields(ledgers, true), ListBit(ledgers, 0));

    ReplicationImage sent(component);
    ImageOf(component, RosterOf(1, { 5, 6 }), sent);
    const std::uint64_t publicRuns = ReplicationVisibleElementRuns(component.Lists[0], false);
    const std::vector<std::uint64_t> elements(2, publicRuns);
    const std::vector<std::byte> bytes =
        Encode(component, sent, ReplicationVisibleFields(component, false), elements);
    ReplicationImage received(component);
    NetBitReader reader(bytes);
    ASSERT_EQ(ReplicationDecodeImage(component, reader, received), ReplicationDecodeError::None);
    const std::span<const RosterEntryWire> held = received.List<RosterEntryWire>(0);
    ASSERT_EQ(held.size(), 2u);
    EXPECT_EQ(held[1].Value, 6u);
    EXPECT_EQ(held[1].Secret, 0u) << "an owner-only run never reaches another peer";
}

TEST(ReplicationCodec, AListAskedPastItsMostIsClampedAndCounted)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ReplicationImage image(component);
    Roster local;
    local.Count = kRosterMost;
    ImageOf(component, local, image);
    EXPECT_EQ(image.Count(0), kRosterMost);
    EXPECT_EQ(image.Clamped(), 0u);

    local.Count = kRosterMost + 1;
    ImageOf(component, local, image);
    EXPECT_EQ(image.Count(0), kRosterMost);
    EXPECT_EQ(image.Clamped(), 1u);

    image.Reset();
    EXPECT_EQ(image.Clamped(), 0u);
    EXPECT_EQ(image.Count(0), 0u);
}

TEST(ReplicationCodec, AnImageThatIsNotWellFormedIsRefused)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ReplicationImage image(component);
    ImageOf(component, RosterOf(4, { 1, 2, 3 }), image);
    const std::vector<std::byte> good(image.Bytes().begin(), image.Bytes().end());

    ReplicationImage other(component);
    ASSERT_TRUE(other.Assign(good));
    EXPECT_EQ(other.Count(0), 3u);

    std::vector<std::byte> trailing = good;
    trailing.resize(good.size() + 8);
    EXPECT_FALSE(other.Assign(trailing)) << "bytes past the last list";

    std::vector<std::byte> truncated = good;
    truncated.resize(good.size() - 1);
    EXPECT_FALSE(other.Assign(truncated));

    std::vector<std::byte> oversized = good;
    const std::uint32_t claimed = kRosterMost + 1;
    std::memcpy(oversized.data() + ReplicationImageHeadBytes(component), &claimed, sizeof(claimed));
    EXPECT_FALSE(other.Assign(oversized)) << "a count above the list's most";
    EXPECT_EQ(other.Count(0), 3u) << "a refused image leaves the last good one";
}

TEST(ReplicationCodec, AListsWidestEncodingBoundsEveryList)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    Roster full;
    full.Label = 0xFFFFFFFFu;
    full.Count = kRosterMost;
    for (RosterEntry& entry : full.Entries)
        entry = RosterEntry{ .Value = 0xFFFFFFFFu, .Secret = 0xFFFFFFFFu, .Target = {} };
    ReplicationImage image(component);
    ImageOf(component, full, image);
    const std::vector<std::byte> bytes = EncodeAll(component, image);
    EXPECT_LE(bytes.size() * 8, ReplicationMaxComponentBits(component) + 7);
    EXPECT_LE((ReplicationMaxComponentBits(component) + 7) / 8, kNetDefaultSnapshotBytes);
}

TEST(ReplicationCodec, AListsChangedElementRunsAreTheOnesThatMoved)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    ReplicationImage before(component);
    ImageOf(component, RosterOf(1, { 1, 2 }), before);
    Roster changed = RosterOf(1, { 1, 2 });
    changed.Entries[1].Secret = 77;
    ReplicationImage after(component);
    ImageOf(component, changed, after);
    const ReplicatedList& list = component.Lists[0];
    EXPECT_EQ(ReplicationChangedElementRuns(list, after.Elements(0).subspan(0, list.Stride),
                                            before.Elements(0).subspan(0, list.Stride)),
              0u);
    EXPECT_EQ(ReplicationChangedElementRuns(list, after.Elements(0).subspan(list.Stride, list.Stride),
                                            before.Elements(0).subspan(list.Stride, list.Stride)),
              0b010u);
    EXPECT_EQ(ReplicationChangedFields(component, after.Bytes(), before.Bytes()), ListBit(component, 0));
}

TEST(NetDesyncFold, AListFoldsItsCountAndElements)
{
    RosterLayout roster;
    const ReplicatedComponent& component = *roster.Component;
    const auto fold = [&](const Roster& value, bool forOwner) {
        ReplicationImage image(component);
        ImageOf(component, value, image);
        return ReplicationFoldVisible(0, component, image.Bytes(), forOwner);
    };
    const Roster base = RosterOf(2, { 1, 2, 3 });
    EXPECT_EQ(fold(base, true), fold(RosterOf(2, { 1, 2, 3 }), true));
    EXPECT_NE(fold(base, false), fold(RosterOf(2, { 1, 2 }), false)) << "the count";
    EXPECT_NE(fold(base, false), fold(RosterOf(2, { 1, 2, 4 }), false)) << "an element";

    Roster secret = base;
    secret.Entries[0].Secret = 1234;
    EXPECT_NE(fold(base, true), fold(secret, true)) << "the owner folds its own run";
    EXPECT_EQ(fold(base, false), fold(secret, false)) << "a run this peer is never sent is not folded";

    const ReplicatedComponent& ledgers = roster.Ledgers();
    const auto foldLedger = [&](std::uint32_t count, bool forOwner) {
        Ledger ledger;
        ledger.Count = count;
        ReplicationImage image(ledgers);
        image.Reset();
        ledgers.ToWire(ReplicationWireContext{}, std::span(reinterpret_cast<const std::byte*>(&ledger), sizeof(ledger)),
                       image);
        return ReplicationFoldVisible(0, ledgers, image.Bytes(), forOwner);
    };
    EXPECT_EQ(foldLedger(1, false), foldLedger(3, false)) << "an invisible list's count is not folded";
    EXPECT_NE(foldLedger(1, true), foldLedger(3, true));
}
