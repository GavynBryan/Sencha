#include "selection/SelectionContext.h"
#include "selection/SelectionService.h"
#include "selection/commands/SelectCommand.h"

#include <gtest/gtest.h>

namespace
{
SelectableRef EntityRef(std::uint32_t index)
{
    return SelectableRef::EntitySelection(RegistryId::Global(), EntityId{ index, 1 });
}
}

TEST(SelectionContext, OrderedSetOperationsMaintainPrimary)
{
    SelectionContext context;
    SelectionService service(context);

    const SelectableRef a = EntityRef(1);
    const SelectableRef b = EntityRef(2);

    service.AddSelection(a);
    service.AddSelection(b);
    service.AddSelection(a);

    ASSERT_EQ(service.GetSelection().size(), 2u);
    EXPECT_EQ(service.GetSelection()[0], a);
    EXPECT_EQ(service.GetSelection()[1], b);
    EXPECT_EQ(service.GetPrimarySelection(), a);

    service.ToggleSelection(a);
    ASSERT_EQ(service.GetSelection().size(), 1u);
    EXPECT_EQ(service.GetPrimarySelection(), b);

    service.ClearSelection();
    EXPECT_TRUE(service.GetSelection().empty());
    EXPECT_FALSE(service.GetPrimarySelection().IsValid());
}

TEST(SelectionContext, SelectCommandRestoresFullSnapshot)
{
    SelectionContext context;
    SelectionService service(context);

    const SelectableRef a = EntityRef(1);
    const SelectableRef b = EntityRef(2);
    const SelectableRef c = EntityRef(3);

    service.SetSelection({ a, b });
    SelectCommand command(service, SelectionSnapshot{ .Items = { c }, .Primary = c });

    command.Execute();
    ASSERT_EQ(service.GetSelection().size(), 1u);
    EXPECT_EQ(service.GetPrimarySelection(), c);

    command.Undo();
    ASSERT_EQ(service.GetSelection().size(), 2u);
    EXPECT_EQ(service.GetSelection()[0], a);
    EXPECT_EQ(service.GetSelection()[1], b);
    EXPECT_EQ(service.GetPrimarySelection(), b);
}
