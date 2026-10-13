#include "properties/PropertySource.h"
#include "properties/SettingProperties.h"

#include <gtest/gtest.h>

// A commit-only source begins an edit on what its nodes show as writable, and
// on nothing else; a read-only one on nothing at all.
namespace
{
class Sheet final : public CommitOnlyPropertySource
{
public:
    [[nodiscard]] std::vector<PropertyNode> Nodes() const override
    {
        return { GroupProperty("group", "Group",
                               { TextProperty("name", "Name", "Atrium"), NoteProperty("id", "Id", "0x2a") }) };
    }
    PropertyEditResult CommitEdit(std::string_view, std::string_view) override { return PropertyEditResult::Applied; }
};

class Readout final : public ReadOnlyPropertySource
{
public:
    [[nodiscard]] std::vector<PropertyNode> Nodes() const override { return { TextProperty("name", "Name", "x") }; }
};
} // namespace

TEST(PropertySource, ACommitOnlySourceEditsWhatItShowsAsWritable)
{
    Sheet sheet;
    EXPECT_TRUE(sheet.BeginEdit("name")) << "nested under a group, still found";
    EXPECT_FALSE(sheet.BeginEdit("id")) << "a note is read-only";
    EXPECT_FALSE(sheet.BeginEdit("missing"));
    EXPECT_EQ(sheet.PreviewEdit("name", "Hall"), PropertyEditResult::Unchanged);

    Readout readout;
    EXPECT_FALSE(readout.BeginEdit("name"));
    EXPECT_EQ(readout.CommitEdit("name", "y"), PropertyEditResult::Refused);
}
