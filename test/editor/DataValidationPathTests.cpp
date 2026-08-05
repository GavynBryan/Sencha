#include <gtest/gtest.h>

#include "DataDocument.h"

#include <vector>

// A validation error is only actionable if it lands on the field that is wrong.
// Subtype compilers already format their errors with the JSON path in front;
// these pin the recovery of that path and the lookup a form does with it.

TEST(SplitCompileError, RecoversThePathACompilerPutInFront)
{
    const DataValidationError error = SplitCompileError(
        "$.data.contexts[0].bindings[2].left names no control: 'key.aa'");

    EXPECT_EQ(error.Path, "$.data.contexts[0].bindings[2].left");
    EXPECT_EQ(error.Message, "names no control: 'key.aa'");
}

TEST(SplitCompileError, HandlesAContextLevelPath)
{
    const DataValidationError error = SplitCompileError(
        "$.data.contexts[1].priority 10 is already used by another context");

    EXPECT_EQ(error.Path, "$.data.contexts[1].priority");
    EXPECT_EQ(error.Message, "10 is already used by another context");
}

TEST(SplitCompileError, FallsBackToTheDocumentWhenNoPathLeads)
{
    // Not every compiler leads with a path, and a message that loses its text
    // would be worse than one reported against the document root.
    const DataValidationError error =
        SplitCompileError("action set is missing or is not an input.actions");

    EXPECT_EQ(error.Path, "$.data");
    EXPECT_EQ(error.Message, "action set is missing or is not an input.actions");
}

TEST(SplitCompileError, KeepsAPathOnlyMessageAsThePath)
{
    const DataValidationError error = SplitCompileError("$.data.actions");

    EXPECT_EQ(error.Path, "$.data.actions");
    EXPECT_TRUE(error.Message.empty());
}

TEST(FindValidationError, MatchesTheExactFieldAndAnythingUnderIt)
{
    const std::vector<DataValidationError> errors{
        { "$.data.contexts[0].bindings[1].control", "names no control" },
    };

    // A collapsed card has to show that something inside it is broken, or the
    // author closes it and never learns.
    EXPECT_NE(FindValidationErrorAt(errors, "$.data.contexts[0].bindings[1].control"), nullptr);
    EXPECT_NE(FindValidationErrorAt(errors, "$.data.contexts[0].bindings[1]"), nullptr);
    EXPECT_NE(FindValidationErrorAt(errors, "$.data.contexts[0]"), nullptr);
    EXPECT_NE(FindValidationErrorAt(errors, "$.data"), nullptr);
}

TEST(FindValidationError, DoesNotMatchASiblingSharingAPrefix)
{
    const std::vector<DataValidationError> errors{
        { "$.data.contexts[0]", "boom" },
    };

    // "$.data.context" is a prefix of "$.data.contexts[0]" as text, but names a
    // different field; only a '.' or '[' continues a path.
    EXPECT_EQ(FindValidationErrorAt(errors, "$.data.context"), nullptr);
    EXPECT_EQ(FindValidationErrorAt(errors, "$.data.contexts[1]"), nullptr);
}

TEST(FindValidationError, ReportsNothingWhenTheDocumentIsClean)
{
    EXPECT_EQ(FindValidationErrorAt({}, "$.data"), nullptr);
}
