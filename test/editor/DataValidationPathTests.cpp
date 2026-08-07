#include <gtest/gtest.h>

#include "DataDocument.h"

#include <core/json/JsonParser.h>

#include <memory>
#include <optional>
#include <string>
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

// ---------------------------------------------------------------------------
// What a subtype compiler is allowed to assume
// ---------------------------------------------------------------------------

namespace
{
// A subtype whose data is one required array, and a compiler that counts its
// calls. Subtype compilers come from the loaded game module, so this stands in
// for arbitrary code the editor runs in-process.
struct CountingSubtype
{
    static constexpr std::string_view kTypeName = "test.counting";

    DataAssetTypeRegistry Types;
    DataSchemaRegistry Schemas;
    std::shared_ptr<int> Compiles = std::make_shared<int>(0);

    CountingSubtype()
    {
        DataFieldSchema element;
        element.Key = "entry";
        element.Kind = DataFieldKind::String;

        DataFieldSchema entries;
        entries.Key = "entries";
        entries.Kind = DataFieldKind::Array;
        entries.Children.push_back(element);

        DataSchema schema;
        schema.TypeName = std::string(kTypeName);
        schema.Root.Kind = DataFieldKind::Record;
        schema.Root.Children.push_back(entries);
        EXPECT_TRUE(Schemas.Register(std::move(schema)));

        std::shared_ptr<int> compiles = Compiles;
        DataAssetTypeRegistration registration;
        registration.Name = std::string(kTypeName);
        registration.CurrentVersion = 1;
        registration.Compile = [compiles](const JsonValue& data) {
            ++*compiles;
            // Written the way a compiler may be written once its schema
            // guarantees the shape. Reached with raw json, this throws.
            const JsonValue* entriesValue = data.Find("entries");
            EXPECT_NE(entriesValue, nullptr);
            (void)entriesValue->AsArray();
            return DataAssetCompileResult{ std::make_shared<int>(1), {}, {} };
        };
        EXPECT_TRUE(Types.Register(std::move(registration)));
    }

    std::unique_ptr<DataDocument> Document()
    {
        return DataDocument::Create("counting_test.sdata", "asset://data/counting_test.sdata",
                                    *Types.Find(kTypeName), *Schemas.Find(kTypeName));
    }
};

// Replaces the document's `data` with a parsed literal.
void SetData(DataDocument& document, std::string_view json)
{
    JsonValue root = document.CopyRoot();
    const std::optional<JsonValue> data = JsonParse(json);
    ASSERT_TRUE(data.has_value());
    *root.Find("data") = *data;
    document.ReplaceRoot(std::move(root));
}
}

TEST(DataDocumentValidation, RunsTheCompilerOnDataItsSchemaAccepted)
{
    CountingSubtype subtype;
    std::unique_ptr<DataDocument> document = subtype.Document();
    ASSERT_NE(document, nullptr);

    SetData(*document, R"({ "entries": ["a"] })");
    document->Validate(subtype.Types, subtype.Schemas);

    EXPECT_TRUE(document->ValidationErrors().empty());
    EXPECT_EQ(*subtype.Compiles, 1);
}

TEST(DataDocumentValidation, DoesNotRunTheCompilerOnDataItsSchemaRejected)
{
    CountingSubtype subtype;
    std::unique_ptr<DataDocument> document = subtype.Document();
    ASSERT_NE(document, nullptr);

    // An author mid-edit, with a field holding the wrong kind of value. The
    // runtime loader would never hand this to a compiler, and the editor must
    // not either: a compiler written against its schema reaches for an array
    // here and takes the editor down with it.
    SetData(*document, R"({ "entries": "not-an-array" })");
    document->Validate(subtype.Types, subtype.Schemas);

    EXPECT_EQ(*subtype.Compiles, 0);
    EXPECT_NE(FindValidationErrorAt(document->ValidationErrors(), "$.data.entries"), nullptr)
        << "the author still learns what is wrong";
}
