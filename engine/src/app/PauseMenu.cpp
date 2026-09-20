#include <app/PauseMenu.h>

#include <authored/VerbBinding.h>
#include <ui/UiAction.h>
#include <ui/UiService.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace
{
    // Positional, and declared beside the description that declares them.
    constexpr UiModelPropertyId kTitle = UiModelPropertyId{ 1 };
    constexpr UiModelArrayId kEntries = UiModelArrayId{ 1 };
    constexpr UiActionId kActivate = UiActionId{ 1 };

    // The model name the engine's own document declares. A page pushed over it
    // must not reuse this: one data model per name per context.
    constexpr const char* kRootModel = "pause";
}

PauseMenu::PauseMenu(UiService& ui, UiSurfaceId surface, PauseState& pause, BackRouter& router)
    : Ui(ui)
    , Surface(surface)
    , Pause(pause)
    , Router(router)
{
    // Last in the order, permanently: "Back opens the menu" is the absence of
    // anyone else wanting it rather than a claim over them.
    Fallback = Router.AddConsumer("pause_shell", BackPriority::Fallback, [this] {
        if (IsOpen())
            return false;
        Open();
        return true;
    });
}

PauseMenu::~PauseMenu()
{
    // Before the leases go, so a consumer cannot be called against a half-torn
    // menu, and before the UI service outlives screens it still holds.
    while (!Pages.empty())
        CloseTop();
}

UiScreenDesc PauseMenu::DescribeRoot() const
{
    UiScreenDesc desc;
    desc.PackagePath = Model_.RootPage();
    desc.ModelName = kRootModel;
    desc.Modal = true;
    desc.Properties = { UiModelProperty{ "title", UiValue(Model_.Title()) } };
    desc.Arrays = { "entries" };
    // The document repeats over the entries and reports which row was
    // activated. It names no command, so adding, renaming or reordering one is
    // a change to the model and never an edit to the markup.
    desc.Actions = { "pause_activate" };
    return desc;
}

void PauseMenu::OpenTop()
{
    if (Pages.empty())
        return;
    OpenPage& page = Pages.back();
    if (page.Screen.IsValid())
        return;

    page.Screen = Ui.OpenScreen(Surface, page.Desc);
}

void PauseMenu::CloseTop()
{
    if (Pages.empty())
        return;

    // Before the screen goes, so a page committing something durable is still
    // looking at the state it presented. Moved out first: a handler that
    // reaches back into the stack must not find the entry it is running from.
    if (std::function<void()> closed = std::move(Pages.back().Closed); closed)
        closed();

    if (Pages.back().Screen.IsValid())
        Ui.CloseScreen(Pages.back().Screen);
    Pages.pop_back();

    // The shell's claim on Back lasts exactly as long as a page does.
    if (Pages.empty())
        Shell.Reset();
}

void PauseMenu::Open()
{
    if (IsOpen())
        return;

    Pause.Request(PausePhase::Paused);

    OpenPage root;
    root.ModelName = kRootModel;
    root.Desc = DescribeRoot();
    Pages.push_back(std::move(root));
    ModelDirty = true;

    // Above game UI now that something is open: an inventory left behind the
    // menu must not take the player's Resume press and close itself instead.
    Shell = Router.AddConsumer("pause_pages", BackPriority::Shell, [this] {
        return IsOpen() && Back();
    });

    OpenTop();
    PublishTop();
}

void PauseMenu::Close()
{
    while (!Pages.empty())
        CloseTop();
    Pause.Request(PausePhase::Playing);
}

void PauseMenu::Push(Page page)
{
    OpenPage open;
    open.ModelName = page.Desc.ModelName;
    open.Desc = std::move(page.Desc);
    open.Publish = std::move(page.Publish);
    open.Activate = std::move(page.Activate);
    open.Closed = std::move(page.Closed);
    Pages.push_back(std::move(open));
    OpenTop();

    // Its first content, now rather than next frame: a page that opened empty
    // and filled in a frame later is a page that flickers.
    if (Pages.back().Screen.IsValid() && Pages.back().Publish)
        Pages.back().Publish(Pages.back().Screen);
}

void PauseMenu::Pop()
{
    if (Pages.size() <= 1)
        return;
    CloseTop();
    // Back on what is underneath again, which for the root means republishing
    // whatever the model now says.
    ModelDirty = true;
    PublishTop();
}

bool PauseMenu::Back()
{
    if (Pages.empty())
    {
        Open();
        return true;
    }
    if (Pages.size() == 1)
    {
        Close();
        return true;
    }
    Pop();
    return true;
}

void PauseMenu::PublishTop()
{
    if (Pages.empty() || Pages.back().ModelName != kRootModel)
        return;
    const UiScreenHandle screen = Pages.back().Screen;
    if (!screen.IsValid())
        return;

    if (!ModelDirty)
        return;
    (void)Ui.SetValue(screen, kTitle, UiValue(Model_.Title()));
    (void)Ui.SetArray(screen, kEntries, Model_.Labels());

    // Captured with the labels, because they are two halves of one published
    // state: the document repeats over these rows and reports positions in
    // them, and a position only means a command while it is the ordering the
    // player is looking at.
    PublishedCommands.clear();
    PublishedCommands.reserve(Model_.Entries().size());
    for (const PauseMenuEntry& entry : Model_.Entries())
        PublishedCommands.push_back(entry.Command);

    ModelDirty = false;
}

void PauseMenu::Dispatch(PauseCommandId command)
{
    if (!command.IsValid() || !Model_.IsEnabled(command))
        return;

    // One behaviour per entry, decided by the model rather than here: an entry
    // with an authored binding has no native handler, and the reverse. Which
    // operation the binding names, and whether it is available, is the
    // dispatcher's answer -- this layer never learns either.
    if (const VerbBindingKey binding = Model_.Binding(command); binding.IsValid())
    {
        LastAdmission = VerbAdmission::Unavailable;
        if (Verbs == nullptr || VerbBindings == nullptr)
            return;
        const CompiledVerbBinding* compiled = VerbBindings->Find(binding);
        if (compiled == nullptr)
        {
            LastAdmission = VerbAdmission::UnresolvedBinding;
            return;
        }
        LastAdmission = Verbs->Invoke(*compiled, {}).Status;
        return;
    }

    if (const PauseCommandHandler* handler = Model_.Handler(command); handler != nullptr)
    {
        PauseMenuContext ctx{ *this };
        (*handler)(ctx);
    }
}

void PauseMenu::Update(RuntimeFrameLoop& runtime, InputContextSet& contexts)
{
    if (!Pages.empty() && Pages.back().Screen.IsValid())
    {
        for (const UiAction& action : Ui.DrainActions(Pages.back().Screen))
        {
            // A page's first declared action is its activation; the row it
            // reports is the index into whatever that page published.
            if (action.Id != kActivate)
                continue;
            const auto index = action.Arguments.empty()
                ? std::size_t{ 0 }
                : static_cast<std::size_t>(std::max<std::int64_t>(0, action.Arguments[0].AsInt()));

            if (Pages.back().ModelName == kRootModel)
            {
                Dispatch(index < PublishedCommands.size() ? PublishedCommands[index]
                                                          : PauseCommandId{});
            }
            else if (Pages.back().Activate)
            {
                const UiValue value =
                    action.Arguments.size() > 1 ? action.Arguments[1] : UiValue{};
                Pages.back().Activate(index, value);
                // Acting on a setting changes what the page shows.
                if (Pages.back().Screen.IsValid() && Pages.back().Publish)
                    Pages.back().Publish(Pages.back().Screen);
            }
            // Dispatching may have popped the page this was drained from.
            if (Pages.empty())
                break;
        }
    }

    // After the dispatch that asked for it, because popping the page a handler
    // is running inside would destroy the model it was dispatched from.
    if (ResumeRequested)
    {
        ResumeRequested = false;
        Close();
    }

    // The transition, resolved here because this is the second of the two
    // points it can be: a Resume drained above lands in the same frame rather
    // than the next one.
    (void)Pause.Apply(runtime, contexts);

    PublishTop();
}
