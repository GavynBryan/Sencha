// The editor operation registry: what every client gets back from a call, and
// that registration, retirement and the console surface move together.

#include "operations/EditorOperations.h"
#include "operations/OperationArguments.h"
#include "operations/OperationReference.h"
#include "operations/OperationScript.h"

#include <core/console/ConsoleService.h>
#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // measure.echo {length: Float = 1, label: String} -> {length, label}
    EditorOperationEntry Echo(std::string name = "measure.echo")
    {
        EditorOperationEntry entry;
        entry.Definition.Name = std::move(name);
        entry.Definition.Description = "Answers with its arguments.";

        DataFieldSchema length = MakeDataField(DataFieldKind::Float, "length", "Length");
        length.Default = 1.0;
        length.Numeric.Minimum = 0.0;
        DataFieldSchema label = MakeDataField(DataFieldKind::String, "label", "Label");
        entry.Definition.Parameters.Children = { length, label };
        entry.Definition.Result.Children = { length, label };

        entry.Execute = [](const JsonValue& arguments) {
            return EditorOperationOutcome::Ok(arguments);
        };
        return entry;
    }

    std::vector<EditorOperationEntry> One(EditorOperationEntry entry)
    {
        std::vector<EditorOperationEntry> entries;
        entries.push_back(std::move(entry));
        return entries;
    }

    JsonValue Parse(std::string_view text)
    {
        return JsonParse(text).value_or(JsonValue{});
    }

    std::string Status(const JsonValue& answer)
    {
        const JsonValue* status = answer.Find("status");
        return status != nullptr && status->IsString() ? status->AsString() : std::string{};
    }

    std::string Code(const JsonValue& answer)
    {
        const JsonValue* code = answer.Find("code");
        return code != nullptr && code->IsString() ? code->AsString() : std::string{};
    }
}

TEST(EditorOperations, AnswersCarryTheCheckedArgumentsWithDefaultsFilled)
{
    EditorOperations operations;
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(Echo()), errors)) << errors.front();

    const JsonValue answer = operations.Invoke("measure.echo", Parse(R"({"label": "hall"})"));
    EXPECT_EQ(Status(answer), "ok");
    EXPECT_EQ(answer.Find("operation")->AsString(), "measure.echo");
    const JsonValue* data = answer.Find("data");
    ASSERT_NE(data, nullptr);
    EXPECT_DOUBLE_EQ(data->Find("length")->AsNumber(), 1.0);
    EXPECT_EQ(data->Find("label")->AsString(), "hall");
}

TEST(EditorOperations, ProblemsAreReportedAsCodesAndPathsNotProse)
{
    EditorOperations operations;
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(Echo()), errors));

    const JsonValue unknown = operations.Invoke("measure.nothing", Parse("{}"));
    EXPECT_EQ(Status(unknown), "error");
    EXPECT_EQ(Code(unknown), "operation.unknown");

    const JsonValue invalid =
        operations.Invoke("measure.echo", Parse(R"({"length": -2, "colour": "red"})"));
    EXPECT_EQ(Status(invalid), "error");
    EXPECT_EQ(Code(invalid), "operation.invalid_arguments");
    std::vector<std::string> paths;
    for (const JsonValue& problem : invalid.Find("problems")->AsArray())
        paths.push_back(problem.Find("path")->AsString());
    EXPECT_NE(std::ranges::find(paths, "$.length"), paths.end());
    EXPECT_NE(std::ranges::find(paths, "$.label"), paths.end());
    EXPECT_NE(std::ranges::find(paths, "$.colour"), paths.end());
    EXPECT_EQ(invalid.Find("data"), nullptr);
}

TEST(EditorOperations, ARefusalKeepsItsCode)
{
    EditorOperationEntry entry = Echo();
    entry.Execute = [](const JsonValue&) {
        return EditorOperationOutcome::Refused("subject.ambiguous", "two entities are named hall");
    };
    EditorOperations operations;
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(std::move(entry)), errors));

    const JsonValue answer = operations.Invoke("measure.echo", Parse(R"({"label": "hall"})"));
    EXPECT_EQ(Status(answer), "refused");
    EXPECT_EQ(Code(answer), "subject.ambiguous");
    EXPECT_EQ(answer.Find("data"), nullptr);
}

TEST(EditorOperations, AnAnswerOutsideTheDeclaredResultIsTheOperationsDefect)
{
    EditorOperationEntry entry = Echo();
    entry.Execute = [](const JsonValue&) {
        return EditorOperationOutcome::Ok(Parse(R"({"length": "long", "label": "x"})"));
    };
    EditorOperations operations;
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(std::move(entry)), errors));

    const JsonValue answer = operations.Invoke("measure.echo", Parse(R"({"label": "hall"})"));
    EXPECT_EQ(Status(answer), "error");
    EXPECT_EQ(Code(answer), "operation.result_contract");
}

TEST(EditorOperations, RegistrationIsAllOrNothing)
{
    std::vector<EditorOperationEntry> entries;
    entries.push_back(Echo("measure.first"));
    EditorOperationEntry broken = Echo("measure.second");
    broken.Execute = nullptr;
    entries.push_back(std::move(broken));

    EditorOperations operations;
    std::vector<std::string> errors{ "an earlier message" };
    EXPECT_FALSE(operations.Register("test", std::move(entries), errors));
    EXPECT_GT(errors.size(), 1u);
    EXPECT_FALSE(operations.Catalog().Find("measure.first").IsValid());
}

TEST(EditorOperations, RetiredOperationsAnswerAsUnknownAndCanReturn)
{
    EditorOperations operations;
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(Echo()), errors));
    operations.Retire("test");
    EXPECT_EQ(Code(operations.Invoke("measure.echo", Parse(R"({"label": "a"})"))),
              "operation.unknown");

    ASSERT_TRUE(operations.Register("test", One(Echo()), errors));
    EXPECT_EQ(Status(operations.Invoke("measure.echo", Parse(R"({"label": "a"})"))), "ok");
}

TEST(EditorOperations, ConsoleCommandsFollowTheirProvider)
{
    ConsoleService console;
    EditorOperations operations;
    operations.ExposeTo(console.Registry());
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(Echo()), errors));

    const ConsoleResult called = console.ExecuteLine("measure.echo label=hall length=2.5");
    ASSERT_TRUE(called.Succeeded());
    ASSERT_FALSE(called.Output.empty());
    const JsonValue answer = Parse(called.Output.back().Text);
    EXPECT_EQ(Status(answer), "ok");
    EXPECT_DOUBLE_EQ(answer.Find("data")->Find("length")->AsNumber(), 2.5);

    EXPECT_FALSE(console.ExecuteLine("measure.echo length=-1 label=x").Succeeded());

    operations.Retire("test");
    EXPECT_EQ(console.ExecuteLine("measure.echo label=hall").Status, ConsoleStatus::UnknownCommand);
}

TEST(OperationArguments, TextBecomesOneArgumentObject)
{
    struct Case
    {
        const char* Text;
        const char* Expected;
    };
    const Case cases[] = {
        { "", "{}" },
        { "  {point: [0, 1, 0], radius: 8, label: 'east hall'}", R"({"point":[0,1,0],"radius":8,"label":"east hall"})" },
        { "radius=8 point=[0, 1, 0] label='east hall'", R"({"radius":8,"point":[0,1,0],"label":"east hall"})" },
        { "subject=0123456789abcdef flag=true name=\"8\"", R"({"subject":"0123456789abcdef","flag":true,"name":"8"})" },
        { "subject=@Hall shape={w: 1, h: 2}", R"({"subject":"@Hall","shape":{"w":1,"h":2}})" },
    };
    for (const Case& c : cases)
    {
        std::string error;
        const std::optional<JsonValue> arguments = OperationArgumentsFromText(c.Text, error);
        ASSERT_TRUE(arguments.has_value()) << c.Text << ": " << error;
        EXPECT_EQ(JsonStringify(*arguments), JsonStringify(Parse(c.Expected))) << c.Text;
    }
}

TEST(OperationArguments, MalformedTextIsRefused)
{
    for (const char* text : { "radius", "=8", "radius=1 radius=2", "{radius: }",
                              "label='open", "point=[0, 1" })
    {
        std::string error;
        EXPECT_FALSE(OperationArgumentsFromText(text, error).has_value()) << text;
        EXPECT_FALSE(error.empty()) << text;
    }
}

TEST(EditorOperations, ConsoleArgumentsSurviveTheConsoleTokenizer)
{
    ConsoleService console;
    EditorOperations operations;
    operations.ExposeTo(console.Registry());
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(Echo()), errors));

    const ConsoleResult called = console.ExecuteLine("measure.echo {label: 'east hall', length: 3}");
    ASSERT_TRUE(called.Succeeded());
    const JsonValue answer = Parse(called.Output.back().Text);
    EXPECT_EQ(answer.Find("data")->Find("label")->AsString(), "east hall");
}

TEST(OperationScript, AnswersEveryLineInOrderAndCountsErrors)
{
    EditorOperations operations;
    std::vector<std::string> errors;
    ASSERT_TRUE(operations.Register("test", One(Echo()), errors));

    std::istringstream script("# a comment\n"
                              "\n"
                              "measure.echo label='east hall'\n"
                              "measure.nothing\n"
                              "measure.echo {length: 4, label: 'x'}\n"
                              "measure.echo label='open\n");
    std::ostringstream answers;
    EXPECT_EQ(RunOperationScript(script, answers, operations), 2u);

    std::istringstream lines(answers.str());
    std::vector<JsonValue> parsed;
    for (std::string line; std::getline(lines, line);)
        parsed.push_back(Parse(line));
    ASSERT_EQ(parsed.size(), 4u);
    EXPECT_EQ(Status(parsed[0]), "ok");
    EXPECT_EQ(parsed[0].Find("data")->Find("label")->AsString(), "east hall");
    EXPECT_EQ(Code(parsed[1]), "operation.unknown");
    EXPECT_DOUBLE_EQ(parsed[2].Find("data")->Find("length")->AsNumber(), 4.0);
    EXPECT_EQ(Code(parsed[3]), "script.malformed");
}

TEST(OperationReference, ListsEveryOperationByCategoryThenNameWithItsParameters)
{
    EditorOperations operations;
    std::vector<std::string> errors;
    EditorOperationEntry later = Echo("measure.later");
    later.Definition.Category = "Measure";
    EditorOperationEntry earlier = Echo("measure.earlier");
    earlier.Definition.Category = "Measure";
    earlier.Definition.Kind = EditorOperationKind::Mutation;
    std::vector<EditorOperationEntry> entries;
    entries.push_back(std::move(later));
    entries.push_back(std::move(earlier));
    ASSERT_TRUE(operations.Register("test", std::move(entries), errors));

    const std::string reference = OperationReference(operations.Catalog());
    const std::size_t earlierAt = reference.find("- `measure.earlier` mutation — Answers with its arguments.");
    const std::size_t laterAt = reference.find("- `measure.later` query");
    ASSERT_NE(earlierAt, std::string::npos) << reference;
    ASSERT_NE(laterAt, std::string::npos);
    EXPECT_LT(earlierAt, laterAt) << "operations are listed by name, whatever order they registered in";
    EXPECT_NE(reference.find("## Measure"), std::string::npos);
    EXPECT_NE(reference.find("  - `length` number, default 1"), std::string::npos) << reference;
    EXPECT_NE(reference.find("  - answers: length, label"), std::string::npos);
    EXPECT_EQ(reference, OperationReference(operations.Catalog())) << "the same catalog, the same text";
}
