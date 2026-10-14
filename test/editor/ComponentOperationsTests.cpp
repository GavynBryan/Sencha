// component.set: one field of a component, named as entity.describe shows it,
// set as one undo step, with the kind the field already holds.

#include "WorkspaceFixture.h"

#include "document/SpaceComponents.h"
#include "operations/EditorOperations.h"
#include "operations/LevelOperations.h"
#include "operations/LevelSubjects.h"
#include "operations/LevelTarget.h"

#include <core/json/JsonParser.h>
#include <core/json/JsonStringify.h>
#include <world/registry/Registry.h>

#include <gtest/gtest.h>

namespace
{
class ComponentOperationsTest : public WorkspaceTest
{
protected:
    void SetUp() override
    {
        WorkspaceTest::SetUp();
        std::vector<std::string> errors;
        ASSERT_TRUE(Operations.Register("level", LevelOperations(Workspace.Level, Operations), errors));
        Ok("space.create", R"({"name": "Hall", "min": [0, 0, 0], "max": [8, 3, 6]})");
    }

    JsonValue Call(std::string_view name, std::string_view arguments)
    {
        return Operations.Invoke(name, JsonParse(arguments).value_or(JsonValue{}));
    }

    JsonValue Ok(std::string_view name, std::string_view arguments)
    {
        JsonValue answer = Call(name, arguments);
        EXPECT_EQ(answer.Find("status")->AsString(), "ok") << name << ": " << JsonStringify(answer);
        const JsonValue* data = answer.Find("data");
        return data != nullptr ? *data : JsonValue{};
    }

    static std::string Code(const JsonValue& answer)
    {
        const JsonValue* code = answer.Find("code");
        return code != nullptr ? code->AsString() : std::string{};
    }

    float Thickness()
    {
        const EntityId hall = ResolveEntitySubject(Workspace.ActiveDocument(), "@Hall").Entities.front();
        return Scene().GetRegistry().Components.TryGet<Shell>(hall)->Thickness;
    }

    EditorOperations Operations;
};
}

TEST_F(ComponentOperationsTest, AFieldIsSetAndUndone)
{
    const float before = Thickness();
    const JsonValue set = Ok("component.set",
                             R"({"subject": "@Hall", "component": "shell", "field": "thickness", "value": "0.5"})");
    EXPECT_FLOAT_EQ(Thickness(), 0.5f);
    EXPECT_EQ(set.Find("stored")->AsString(), "0.5");
    Commands.Undo();
    EXPECT_FLOAT_EQ(Thickness(), before);
}

TEST_F(ComponentOperationsTest, WhatCannotBeSetRefusesWithWhatCould)
{
    const JsonValue absent = Call("component.set",
                                  R"({"subject": "@Hall", "component": "nope", "field": "x", "value": "1"})");
    EXPECT_EQ(Code(absent), "component.absent");
    EXPECT_NE(JsonStringify(*absent.Find("detail")).find("shell"), std::string::npos) << "the components it has";

    const JsonValue unknown = Call("component.set",
                                   R"({"subject": "@Hall", "component": "shell", "field": "depth", "value": "1"})");
    EXPECT_EQ(Code(unknown), "component.field_unknown");
    EXPECT_NE(JsonStringify(*unknown.Find("detail")).find("thickness"), std::string::npos) << "the fields it has";

    EXPECT_EQ(Code(Call("component.set",
                        R"({"subject": "@Hall", "component": "shell", "field": "thickness", "value": "\"thick\""})")),
              "component.value_invalid");
}
