#include "selection/SelectionFold.h"

#include <gtest/gtest.h>

namespace
{
SelectableRef Ref(std::uint32_t index)
{
    return SelectableRef::EntitySelection(RegistryId::Global(), EntityId{ index, 1 });
}

SelectionSnapshot Snapshot(std::initializer_list<SelectableRef> items)
{
    SelectionSnapshot snapshot;
    snapshot.Items = items;
    snapshot.Primary = snapshot.Items.empty() ? SelectableRef{} : snapshot.Items.back();
    return snapshot;
}
}

TEST(SelectionFold, ReplaceDedupsAndSetsPrimary)
{
    const SelectionSnapshot out = SelectionFold::Apply(
        Snapshot({ Ref(1) }), { Ref(2), Ref(3), Ref(2) }, SelectionFold::Op::Replace);
    ASSERT_EQ(out.Items.size(), 2u);
    EXPECT_EQ(out.Items[0], Ref(2));
    EXPECT_EQ(out.Items[1], Ref(3));
    EXPECT_EQ(out.Primary, Ref(3));
}

TEST(SelectionFold, ReplaceWithEmptyClears)
{
    const SelectionSnapshot out = SelectionFold::Apply(Snapshot({ Ref(1) }), {}, SelectionFold::Op::Replace);
    EXPECT_TRUE(out.Items.empty());
    EXPECT_FALSE(out.Primary.IsValid());
}

TEST(SelectionFold, AddAppendsAndPromotesReclickedPrimary)
{
    SelectionSnapshot out = SelectionFold::Apply(Snapshot({ Ref(1), Ref(2) }), { Ref(3) }, SelectionFold::Op::Add);
    ASSERT_EQ(out.Items.size(), 3u);
    EXPECT_EQ(out.Primary, Ref(3));

    // Adding an already-selected item keeps the set but promotes it to primary.
    out = SelectionFold::Apply(out, { Ref(1) }, SelectionFold::Op::Add);
    ASSERT_EQ(out.Items.size(), 3u);
    EXPECT_EQ(out.Primary, Ref(1));
}

TEST(SelectionFold, ToggleAddsAbsentAndRemovesPresent)
{
    // Absent item: toggles in and becomes primary.
    SelectionSnapshot out = SelectionFold::Apply(Snapshot({ Ref(1) }), { Ref(2) }, SelectionFold::Op::Toggle);
    ASSERT_EQ(out.Items.size(), 2u);
    EXPECT_EQ(out.Primary, Ref(2));

    // Present item: toggles out; primary repairs to a remaining item.
    out = SelectionFold::Apply(out, { Ref(2) }, SelectionFold::Op::Toggle);
    ASSERT_EQ(out.Items.size(), 1u);
    EXPECT_EQ(out.Items[0], Ref(1));
    EXPECT_EQ(out.Primary, Ref(1));
}

TEST(SelectionFold, ToggleMixedSetAddsAndRemovesPerItem)
{
    const SelectionSnapshot out = SelectionFold::Apply(
        Snapshot({ Ref(1), Ref(2) }), { Ref(2), Ref(3) }, SelectionFold::Op::Toggle);
    ASSERT_EQ(out.Items.size(), 2u);
    EXPECT_EQ(out.Items[0], Ref(1));
    EXPECT_EQ(out.Items[1], Ref(3));
    EXPECT_EQ(out.Primary, Ref(3));
}

TEST(SelectionFold, RemoveRepairsPrimary)
{
    SelectionSnapshot current = Snapshot({ Ref(1), Ref(2), Ref(3) });
    current.Primary = Ref(3);
    const SelectionSnapshot out = SelectionFold::Apply(current, { Ref(3) }, SelectionFold::Op::Remove);
    ASSERT_EQ(out.Items.size(), 2u);
    EXPECT_EQ(out.Primary, Ref(2));
}

TEST(SelectionFold, RemoveToEmptyInvalidatesPrimary)
{
    const SelectionSnapshot out = SelectionFold::Apply(Snapshot({ Ref(1) }), { Ref(1) }, SelectionFold::Op::Remove);
    EXPECT_TRUE(out.Items.empty());
    EXPECT_FALSE(out.Primary.IsValid());
}

TEST(SelectionFold, InvalidRefsAreIgnored)
{
    const SelectionSnapshot out = SelectionFold::Apply(
        Snapshot({}), { SelectableRef{}, Ref(1) }, SelectionFold::Op::Replace);
    ASSERT_EQ(out.Items.size(), 1u);
    EXPECT_EQ(out.Primary, Ref(1));
}

TEST(SelectionFold, ClickDecode)
{
    EXPECT_EQ(SelectionFold::OpForClick(false, false), SelectionFold::Op::Replace);
    EXPECT_EQ(SelectionFold::OpForClick(false, true), SelectionFold::Op::Add);
    EXPECT_EQ(SelectionFold::OpForClick(true, false), SelectionFold::Op::Toggle);
    EXPECT_EQ(SelectionFold::OpForClick(true, true), SelectionFold::Op::Remove);
}

TEST(SelectionFold, BulkDecodeRemovesInsteadOfToggling)
{
    EXPECT_EQ(SelectionFold::OpForBulk(false, false), SelectionFold::Op::Replace);
    EXPECT_EQ(SelectionFold::OpForBulk(false, true), SelectionFold::Op::Add);
    EXPECT_EQ(SelectionFold::OpForBulk(true, false), SelectionFold::Op::Remove);
    EXPECT_EQ(SelectionFold::OpForBulk(true, true), SelectionFold::Op::Remove);
}
