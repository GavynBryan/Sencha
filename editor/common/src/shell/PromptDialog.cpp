#include "shell/PromptDialog.h"

#include "prompts/PromptQueue.h"

#include <ui/UiKeys.h>
#include <ui/UiService.h>

#include <optional>
#include <string>
#include <vector>

namespace
{
const UiModelPropertyId kTitle = UiPropertyIdAt(0);
const UiModelPropertyId kMessage = UiPropertyIdAt(1);
const UiModelPropertyId kHasField = UiPropertyIdAt(2);
const UiModelPropertyId kField = UiPropertyIdAt(3);
const UiModelRowsId kItems = UiRowsIdAt(0);
const UiModelRowsId kChoices = UiRowsIdAt(1);
const UiActionId kChooseAction = UiActionIdAt(0);
const UiActionId kKeyAction = UiActionIdAt(1);
const UiActionId kWriteAction = UiActionIdAt(2);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://prompt.rml";
    desc.ModelName = "prompt";
    desc.Modal = true;
    desc.Properties = {
        UiModelProperty{ "title", UiValue(std::string{}) },
        UiModelProperty{ "message", UiValue(std::string{}) },
        UiModelProperty{ "has_field", UiValue(false) },
        UiModelProperty{ "field", UiValue(std::string{}) },
    };
    desc.RowLists = { "items", "choices" };
    desc.Actions = { "prompt_choose", "prompt_key", "prompt_write" };
    return desc;
}

} // namespace

PromptDialog::PromptDialog(UiService& ui, UiSurfaceId surface, PromptQueue& prompts, ActionContextStack& actions)
    : Ui(ui)
    , Prompts(prompts)
    , Actions(actions)
    , Screen(ui, Describe, surface)
{
    EscapeActions.Claim("edit.escape", ActionBinding{ .Invoke = [this] { (void)Prompts.Dismiss(); } });
}

PromptDialog::~PromptDialog()
{
    Hide();
}

void PromptDialog::Update()
{
    if (IsShowing())
    {
        for (const UiAction& action : Ui.DrainActions(Screen.Handle()))
        {
            const std::optional<UiKey> key = action.Id == kKeyAction && !action.Arguments.empty()
                                               ? UiKeyFromEvent(action.Arg(0).AsInt())
                                               : std::nullopt;
            const PromptRequest* front = Prompts.Front();
            if (key == UiKey::Escape)
                (void)Prompts.Dismiss();
            else if (key == UiKey::Return && front != nullptr && front->Field.has_value() && !front->Choices.empty())
                (void)Prompts.Answer(front->Choices.front().Key, Written);
            else if (action.Id == kWriteAction && !action.Arguments.empty())
                Written = action.Arg(0).AsString();
            else if (action.Id == kChooseAction && !action.Arguments.empty())
                (void)Prompts.Answer(action.Arg(0).AsString(), Written);
        }
    }
    if (Prompts.Front() == nullptr)
    {
        Hide();
        return;
    }
    // Opened afresh for each question, so a field it asks for takes the keys.
    if (Prompts.FrontNumber() != Showing)
        Hide();
    Show();
    Publish();
}

void PromptDialog::Show()
{
    if (IsShowing())
        return;
    (void)Screen.Open();
    EscapeLayer = Actions.Push(EscapeActions, ActionRank::Surface);
    Showing = Prompts.FrontNumber();
    Written = Prompts.Front()->Field.value_or(std::string{});
    (void)Ui.SetValue(Screen.Handle(), kHasField, UiValue(Prompts.Front()->Field.has_value()));
    (void)Ui.SetValue(Screen.Handle(), kField, UiValue(Written));
}

void PromptDialog::Hide()
{
    EscapeLayer.Reset();
    Showing = 0;
    Screen.Close();
}

void PromptDialog::Publish()
{
    const PromptRequest& prompt = *Prompts.Front();
    (void)Ui.SetValue(Screen.Handle(), kTitle, UiValue(prompt.Title));
    (void)Ui.SetValue(Screen.Handle(), kMessage, UiValue(prompt.Message));

    std::vector<UiRow> items;
    for (const std::string& item : prompt.Items)
        items.push_back(UiRow{ item, "", "" });
    (void)Ui.SetRows(Screen.Handle(), kItems, items);

    std::vector<UiRow> choices;
    for (const PromptChoice& choice : prompt.Choices)
    {
        UiRow row{ choice.Label, "", std::string(ActionToneName(choice.Tone)) };
        row.Key = choice.Key;
        choices.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen.Handle(), kChoices, choices);
}
