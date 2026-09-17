#include <gtest/gtest.h>

#include "authoring/BindingMisses.h"
#include "authoring/UiPreviewModel.h"

#include <vector>

namespace
{
UiDiagnostic Miss(UiDiagnosticKind kind, const char* variable)
{
    UiDiagnostic d;
    d.Kind = kind;
    d.Source = UiDiagnosticSource::DocumentEngine;
    d.Variable = variable;
    return d;
}
}

TEST(BindingMisses, ClassifiesVariablesMembersAndActionsAndDropsWhatIsDeclared)
{
    UiPreviewModel model;
    model.Properties = { UiModelProperty{ "title", UiValue(std::string{}), false } };
    model.Actions = { "known" };

    const std::vector<UiDiagnostic> diagnostics = {
        Miss(UiDiagnosticKind::BindingMissing, "title"),            // declared: dropped
        Miss(UiDiagnosticKind::BindingMissing, "hint"),             // a variable
        Miss(UiDiagnosticKind::BindingMissing, "rows[0].Label"),    // a member through a list
        Miss(UiDiagnosticKind::MemberMissing, "Label"),             // the same member, tersely
        Miss(UiDiagnosticKind::EventCallbackMissing, "known"),      // declared: dropped
        Miss(UiDiagnosticKind::EventCallbackMissing, "options_activate"),
        Miss(UiDiagnosticKind::DocumentEngineOther, "ignored"),     // not a miss at all
    };
    const std::vector<BindingMiss> misses = CollectBindingMisses(diagnostics, model);
    ASSERT_EQ(misses.size(), 3u);
    EXPECT_EQ(misses[0], (BindingMiss{ BindingMissKind::Variable, "hint", "" }));
    EXPECT_EQ(misses[1], (BindingMiss{ BindingMissKind::Member, "Label", "rows" }));
    EXPECT_EQ(misses[2], (BindingMiss{ BindingMissKind::Action, "options_activate", "" }));
}

TEST(BindingMisses, ATerseMemberReportLearnsItsParentFromAFullerOne)
{
    UiPreviewModel model;
    const std::vector<UiDiagnostic> diagnostics = {
        Miss(UiDiagnosticKind::MemberMissing, "Label"),
        Miss(UiDiagnosticKind::BindingMissing, "rows[2].Label"),
    };
    const std::vector<BindingMiss> misses = CollectBindingMisses(diagnostics, model);
    ASSERT_EQ(misses.size(), 1u);
    EXPECT_EQ(misses.front().Parent, "rows");
}
