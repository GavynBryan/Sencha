#include "data/DataDocument.h"

#include <anim/AnimRequestSchema.h>
#include <core/json/JsonParser.h>

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace
{
class AnimRequestSchemaTest : public testing::Test
{
protected:
    DataAssetTypeRegistry Types;
    DataSchemaRegistry Schemas;
    void SetUp() override { RegisterAnimRequestSchema(Types, Schemas); }

    std::unique_ptr<DataDocument> Document(std::string_view intents)
    {
        auto document = DataDocument::Create("unused-request-schema.sdata", "asset://animation/requests.sdata",
            *Types.Find(kAnimRequestSchemaType), *Schemas.Find(kAnimRequestSchemaType));
        auto root = document->CopyRoot();
        *root.Find("data")->Find("intents") = *JsonParse(intents);
        document->ReplaceRoot(std::move(root));
        document->Validate(Types, Schemas);
        return document;
    }
};
}

TEST_F(AnimRequestSchemaTest, CompilesPortableNamesAndFourTypedParameters)
{
    auto document = Document(R"([{"intent":"Anim.Action","params":[
        {"name":"amount","kind":"float"}, {"name":"index","kind":"int"},
        {"name":"held","kind":"bool"}, {"name":"variant","kind":"tag"}]}])");
    ASSERT_TRUE(document->IsSemanticallyValid());
    const auto compiled = Types.Find(kAnimRequestSchemaType)->Compile(*document->Data());
    const auto value = std::static_pointer_cast<const AnimRequestSchema>(compiled.Value);
    ASSERT_NE(value, nullptr);
    ASSERT_EQ(value->Intents.size(), 1u);
    const auto& intent = value->Intents.front();
    EXPECT_EQ(intent.Intent, "Anim.Action");
    EXPECT_EQ(intent.ParamCount, 4);
    EXPECT_EQ(intent.Params[0].Name, "amount");
    EXPECT_EQ(intent.Params[0].Kind, AnimRequestParamKind::Float);
    EXPECT_EQ(intent.Params[1].Kind, AnimRequestParamKind::Int);
    EXPECT_EQ(intent.Params[2].Kind, AnimRequestParamKind::Bool);
    EXPECT_EQ(intent.Params[3].Kind, AnimRequestParamKind::Tag);
    EXPECT_TRUE(compiled.Dependencies.empty());
}

TEST_F(AnimRequestSchemaTest, FifthParameterHasActionableFieldDiagnostic)
{
    auto document = Document(R"([{"intent":"Anim.Action","params":[
        {"name":"a","kind":"float"},{"name":"b","kind":"int"},
        {"name":"c","kind":"bool"},{"name":"d","kind":"tag"},
        {"name":"e","kind":"float"}]}])");
    ASSERT_EQ(document->ValidationErrors().size(), 1u);
    EXPECT_EQ(document->ValidationErrors()[0].Path, "$.data.intents[0].params");
    EXPECT_NE(document->ValidationErrors()[0].Message.find("facts"), std::string::npos);
}

TEST_F(AnimRequestSchemaTest, DuplicateIntentAndParameterNamesAreRejected)
{
    auto document = Document(R"([{"intent":"Anim.Reload","params":[]},
                                 {"intent":"Anim.Reload","params":[]}])");
    ASSERT_FALSE(document->IsSemanticallyValid());
    EXPECT_EQ(document->ValidationErrors()[0].Path, "$.data.intents[1].intent");
    document = Document(R"([{"intent":"Anim.Action","params":[
        {"name":"a","kind":"float"},{"name":"a","kind":"int"}]}])");
    ASSERT_FALSE(document->IsSemanticallyValid());
    EXPECT_EQ(document->ValidationErrors()[0].Path, "$.data.intents[0].params[1].name");
}

TEST_F(AnimRequestSchemaTest, MalformedShapesAndUnknownKindsNeverReachCompiler)
{
    for (const auto text : {R"([{"intent":12,"params":[]}])",
        R"([{"intent":"Anim.Action","params":null}])",
        R"([{"intent":"Anim.Action","params":[{"name":"x","kind":"entity"}]}])",
        R"([{"intent":"Anim.Action","params":[{"name":"x","kind":0}]}])"})
    {
        auto document = Document(text);
        EXPECT_FALSE(document->IsSemanticallyValid()) << text;
    }
}

TEST_F(AnimRequestSchemaTest, RequestHasNoAuthoredPriorityTargetOrCallback)
{
    for (const auto text : {R"([{"intent":"Anim.Action","params":[],"priority":1}])",
        R"([{"intent":"Anim.Action","params":[],"target":"entity"}])",
        R"([{"intent":"Anim.Action","params":[],"callback":"effect"}])"})
        EXPECT_FALSE(Document(text)->IsSemanticallyValid());
}

TEST_F(AnimRequestSchemaTest, RenameInteractionCoalescesAndUndoPreservesOtherFields)
{
    auto document = Document(R"([{"intent":"Anim.Action","params":[]}])");
    document->BeginEdit();
    for (const char* text : {"Anim.A", "Anim.Aim"})
    {
        auto root = document->CopyRoot();
        *root.Find("data")->Find("intents")->AsArray()[0].Find("intent") = JsonValue(text);
        document->PreviewRoot(std::move(root));
    }
    document->CommitEdit();
    document->Undo();
    EXPECT_EQ(document->Data()->Find("intents")->AsArray()[0].Find("intent")->AsString(), "Anim.Action");
    document->Redo();
    EXPECT_EQ(document->Data()->Find("intents")->AsArray()[0].Find("intent")->AsString(), "Anim.Aim");
    EXPECT_EQ(document->Version(), 1u);
}

TEST_F(AnimRequestSchemaTest, CancelRestoresPreviewWithoutAddingUndoEntry)
{
    auto document = Document(R"([{"intent":"Anim.Action","params":[]}])");
    document->BeginEdit();
    auto root = document->CopyRoot();
    root.Find("data")->Find("intents")->AsArray().clear();
    document->PreviewRoot(std::move(root));
    document->CancelEdit();
    EXPECT_EQ(document->Data()->Find("intents")->Size(), 1u);
    document->Undo();
    EXPECT_EQ(document->Data()->Find("intents")->Size(), 0u);
}

TEST_F(AnimRequestSchemaTest, DefaultDocumentIsAnEmptyValidSchema)
{
    auto document = Document("[]");
    EXPECT_TRUE(document->IsSemanticallyValid());
    RegisterAnimRequestSchema(Types, Schemas);
    EXPECT_EQ(Types.Entries().size(), 1u);
    EXPECT_EQ(Schemas.Entries().size(), 1u);
}

TEST_F(AnimRequestSchemaTest, SaveDoesNotReportItsOwnWriteAsAnExternalEdit)
{
    const auto path = std::filesystem::temp_directory_path() / "sencha_request_schema_save_test.sdata";
    struct Cleanup
    {
        std::filesystem::path Path;
        ~Cleanup() { std::error_code error; std::filesystem::remove(Path, error); }
    } cleanup{path};
    auto document = DataDocument::Create(path, "asset://animation/save_test.sdata",
        *Types.Find(kAnimRequestSchemaType), *Schemas.Find(kAnimRequestSchemaType));
    std::string error;
    ASSERT_TRUE(document->Save(&error)) << error;
    EXPECT_FALSE(document->IsDirty());
    EXPECT_FALSE(document->IsExternallyModified());
    auto root = document->CopyRoot();
    *root.Find("data")->Find("intents") = *JsonParse(R"([{"intent":"Anim.Reload","params":[]}])");
    document->ReplaceRoot(std::move(root));
    ASSERT_TRUE(document->Save(&error)) << error;
    EXPECT_FALSE(document->IsExternallyModified());
    const auto reopened = DataDocument::Open(path, document->VirtualPath(), Types, Schemas, &error);
    ASSERT_NE(reopened, nullptr) << error;
    ASSERT_TRUE(reopened->IsSemanticallyValid());
    EXPECT_EQ(reopened->Data()->Find("intents")->Size(), 1u);
}
