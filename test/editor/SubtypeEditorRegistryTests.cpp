#include <gtest/gtest.h>

#include "SubtypeEditorRegistry.h"

#include <string>
#include <utility>

// The data editor core asks this registry what to do with a document and never
// names a subtype itself. What matters is that a claim is honoured, a second
// claim on the same subtype is refused, and an unclaimed subtype reports
// nothing so the caller falls back to the schema-generated form.

namespace
{
class FakeSubtypeEditor final : public IDataSubtypeEditor
{
public:
    explicit FakeSubtypeEditor(std::string subtype)
        : Name(std::move(subtype))
    {
    }

    [[nodiscard]] std::string_view Subtype() const override { return Name; }
    [[nodiscard]] FieldEdit DrawForm(SubtypeFormContext&) override { return {}; }

private:
    std::string Name;
};

std::unique_ptr<IDataSubtypeEditor> MakeEditor(std::string subtype)
{
    return std::make_unique<FakeSubtypeEditor>(std::move(subtype));
}
}

TEST(SubtypeEditorRegistry, FindsARegisteredEditorBySubtype)
{
    SubtypeEditorRegistry registry;
    ASSERT_TRUE(registry.Register(MakeEditor("movement.profile")));
    ASSERT_TRUE(registry.Register(MakeEditor("input.profile")));

    const IDataSubtypeEditor* movement = registry.Find("movement.profile");
    ASSERT_NE(movement, nullptr);
    EXPECT_EQ(movement->Subtype(), "movement.profile");
    EXPECT_NE(registry.Find("input.profile"), nullptr);
    EXPECT_EQ(registry.Entries().size(), 2u);
}

TEST(SubtypeEditorRegistry, RefusesASecondEditorForOneSubtype)
{
    SubtypeEditorRegistry registry;
    ASSERT_TRUE(registry.Register(MakeEditor("input.profile")));

    // Two surfaces claiming one document would both draw it and race for its
    // platform events, so the later claim loses rather than shadowing.
    EXPECT_FALSE(registry.Register(MakeEditor("input.profile")));
    EXPECT_EQ(registry.Entries().size(), 1u);
}

TEST(SubtypeEditorRegistry, ReportsNothingForAnUnclaimedSubtype)
{
    SubtypeEditorRegistry registry;
    ASSERT_TRUE(registry.Register(MakeEditor("movement.profile")));

    // The caller's cue to draw the schema-generated form instead.
    EXPECT_EQ(registry.Find("input.profile"), nullptr);
    EXPECT_EQ(registry.Find(""), nullptr);
}

TEST(SubtypeEditorRegistry, RejectsAnEditorThatClaimsNothing)
{
    SubtypeEditorRegistry registry;
    EXPECT_FALSE(registry.Register(nullptr));
    EXPECT_FALSE(registry.Register(MakeEditor("")));
    EXPECT_TRUE(registry.Entries().empty());
}

TEST(SubtypeEditorRegistry, KeepsRegistrationOrder)
{
    SubtypeEditorRegistry registry;
    ASSERT_TRUE(registry.Register(MakeEditor("a.one")));
    ASSERT_TRUE(registry.Register(MakeEditor("b.two")));

    // Panels are created by walking this, so a stable order keeps the dock
    // layout from shuffling between runs.
    const auto entries = registry.Entries();
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0]->Subtype(), "a.one");
    EXPECT_EQ(entries[1]->Subtype(), "b.two");
}
