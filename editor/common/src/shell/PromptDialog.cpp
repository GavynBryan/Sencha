#include "shell/PromptDialog.h"

#include "prompts/PromptQueue.h"

#include <ui/UiKeys.h>
#include <ui/UiService.h>

#include <string>
#include <vector>

namespace
{
const UiModelPropertyId kTitle = UiPropertyIdAt(0);
const UiModelPropertyId kMessage = UiPropertyIdAt(1);
const UiModelRowsId kItems = UiRowsIdAt(0);
const UiModelRowsId kChoices = UiRowsIdAt(1);
const UiActionId kChooseAction = UiActionIdAt(0);
const UiActionId kKeyAction = UiActionIdAt(1);

UiScreenDesc Describe()
{
    UiScreenDesc desc;
    desc.PackagePath = "asset://prompt.rml";
    desc.ModelName = "prompt";
    desc.Modal = true;
    desc.Properties = {
        UiModelProperty{ "title", UiValue(std::string{}) },
        UiModelProperty{ "message", UiValue(std::string{}) },
    };
    desc.RowLists = { "items", "choices" };
    desc.Actions = { "prompt_choose", "prompt_key" };
    return desc;
}

const char* ToneName(ActionTone tone)
{
    switch (tone)
    {
    case ActionTone::Destructive: return "destructive";
    case ActionTone::Active:      return "active";
    case ActionTone::Normal:
    default:                      return "normal";
    }
}
} // namespace

PromptDialog::PromptDialog(UiService& ui, UiSurfaceId surface, PromptQueue& prompts, ActionContextStack& actions)
    : Ui(ui)
    , Surface(surface)
    , Prompts(prompts)
    , Actions(actions)
{
    EscapeActions.Claim("edit.escape", ActionHandler{ .Invoke = [this] { (void)Prompts.Dismiss(); } });
}

PromptDialog::~PromptDialog()
{
    Hide();
}

void PromptDialog::Update()
{
    if (IsShowing())
    {
        for (const UiAction& action : Ui.DrainActions(Screen))
        {
            const bool escape = action.Id == kKeyAction && !action.Arguments.empty()
                             && UiKeyFromEvent(action.Arguments[0].AsInt()) == UiKey::Escape;
            if (escape)
                (void)Prompts.Dismiss();
            else if (action.Id == kChooseAction && !action.Arguments.empty())
                (void)Prompts.Answer(action.Arguments[0].AsString());
        }
    }
    if (Prompts.Front() == nullptr)
    {
        Hide();
        return;
    }
    Show();
    Publish();
}

void PromptDialog::Show()
{
    if (IsShowing())
        return;
    Screen = Ui.OpenScreen(Surface, Describe());
    EscapeLayer = Actions.Push(EscapeActions, ActionRank::Surface);
}

void PromptDialog::Hide()
{
    EscapeLayer.Reset();
    if (!Screen.IsValid())
        return;
    Ui.CloseScreen(Screen);
    Screen = {};
}

void PromptDialog::Publish()
{
    const PromptRequest& prompt = *Prompts.Front();
    (void)Ui.SetValue(Screen, kTitle, UiValue(prompt.Title));
    (void)Ui.SetValue(Screen, kMessage, UiValue(prompt.Message));

    std::vector<UiRow> items;
    for (const std::string& item : prompt.Items)
        items.push_back(UiRow{ item, "", "" });
    (void)Ui.SetRows(Screen, kItems, items);

    std::vector<UiRow> choices;
    for (const PromptChoice& choice : prompt.Choices)
    {
        UiRow row{ choice.Label, "", ToneName(choice.Tone) };
        row.Key = choice.Key;
        choices.push_back(std::move(row));
    }
    (void)Ui.SetRows(Screen, kChoices, choices);
}
