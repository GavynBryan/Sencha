#include "workspace/ProposalTray.h"

#include "actions/ActionOffer.h"
#include "document/EditorDocument.h"
#include "notices/NoticeQueue.h"
#include "operations/EditorOperations.h"
#include "operations/LevelTarget.h"
#include "operations/SpaceDiagnostics.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    constexpr std::string_view kDraftKey = "draft:";
    constexpr std::string_view kFileKey = "file:";

    std::string Counted(std::size_t count, std::string_view what)
    {
        return std::to_string(count) + " " + std::string(what);
    }

    // What the draft would change, and how many more or fewer problems the
    // level would have for it.
    std::string Summary(LevelTarget& level, const LevelDraft& draft)
    {
        std::size_t added = 0, changed = 0, removed = 0;
        for (const EntityChange& change : draft.Changes.Changes)
            (!change.Before ? added : !change.After ? removed : changed) += 1;
        std::string summary = draft.Label + " · " + Counted(added, "new") + ", " + Counted(changed, "changed") + ", "
            + Counted(removed, "removed");
        if (draft.State == LevelDraftState::Failed)
            return summary + " · failed";
        if (const EditorDocument* base = level.BaseOf(draft))
        {
            const long delta = static_cast<long>(SpaceDiagnostics(draft.Document->GetScene(), std::nullopt).size())
                - static_cast<long>(SpaceDiagnostics(base->GetScene(), std::nullopt).size());
            if (delta != 0)
                summary += " · " + std::string(delta > 0 ? "+" : "") + std::to_string(delta) + " problems";
        }
        return summary;
    }
}

ProposalTray::ProposalTray(LevelTarget& level, EditorOperations& operations, NoticeQueue& notices,
                           std::filesystem::path proposals)
    : Level(level)
    , Operations(operations)
    , Notices(notices)
    , Proposals(std::move(proposals))
{
}

ExplorerRow ProposalTray::Row()
{
    return { .Id = std::string(kId),
             .Title = "Proposals",
             .Chords = { { SDLK_D, { .Shift = true } } },
             .Explore = [this](const ExplorerQuery& query) { return Explore(query); },
             .Choose = [this](std::string_view key, ExplorerChoice choice, std::size_t) { Choose(key, choice); },
             .Shape = ExplorerShape::Outline };
}

void ProposalTray::Offer(ActionCatalog& catalog, ActionContext& context)
{
    const ActionOffers offer{ catalog, context, "Proposal" };
    const auto shown = [this] { return Level.PreviewedDraft() != nullptr; };
    offer({ .Id = "proposal.accept", .Label = "Accept Proposal",
            .DefaultChords = { { SDLK_RETURN, { .Ctrl = true } } } },
          { .Invoke = [this] { Settle("draft.accept", "Accepted"); }, .Enabled = shown });
    offer({ .Id = "proposal.reject", .Label = "Reject Proposal",
            .DefaultChords = { { SDLK_BACKSPACE, { .Ctrl = true } } } },
          { .Invoke = [this] { Settle("draft.discard", "Rejected"); }, .Enabled = shown });
}

ExplorerPage ProposalTray::Explore(const ExplorerQuery& query)
{
    ExplorerPage page;
    const auto matches = [&](const std::string& label) {
        return query.Filter.empty() || label.find(query.Filter) != std::string::npos;
    };
    std::vector<std::string> open;
    for (const std::unique_ptr<LevelDraft>& draft : Level.Drafts())
    {
        if (!draft->OpenedFrom.empty())
            open.push_back(std::filesystem::path(draft->OpenedFrom).lexically_normal().string());
        const std::string label = Summary(Level, *draft);
        if (matches(label) && CountOnPage(page, query))
            page.Items.push_back({ .Key = std::string(kDraftKey) + draft->Id, .Label = label,
                                   .Current = draft.get() == Level.PreviewedDraft() });
    }

    std::error_code error;
    std::vector<std::filesystem::path> files;
    if (!Proposals.empty())
        for (const auto& entry : std::filesystem::directory_iterator(Proposals, error))
            if (entry.path().extension() == ".sproposal")
                files.push_back(entry.path().lexically_normal());
    std::ranges::sort(files);
    for (const std::filesystem::path& file : files)
    {
        if (std::ranges::find(open, file.string()) != open.end())
            continue;
        const std::string label = file.stem().string() + " · not open";
        if (matches(label) && CountOnPage(page, query))
            page.Items.push_back({ .Key = std::string(kFileKey) + file.string(), .Label = label });
    }

    page.Actions = { "proposal.accept", "proposal.reject" };
    page.Empty = Proposals.empty() ? "No proposals." : "No proposals. Agents write them to " + Proposals.string() + ".";
    return page;
}

void ProposalTray::Choose(std::string_view key, ExplorerChoice choice)
{
    if (choice != ExplorerChoice::Pick && choice != ExplorerChoice::Commit)
        return;
    if (key.starts_with(kDraftKey))
    {
        if (LevelDraft* draft = Level.FindDraft(key.substr(kDraftKey.size())))
            Level.Preview(draft);
        return;
    }
    if (!key.starts_with(kFileKey))
        return;
    const std::string path(key.substr(kFileKey.size()));
    const JsonValue answer =
        Operations.Invoke("draft.open", JsonValue(JsonValue::Object{ { "path", JsonValue(path) } }));
    const JsonValue* message = answer.Find("message");
    if (message != nullptr && message->IsString())
        Notices.Post(message->AsString(), NoticeTone::Warning, NoticeClockNow());
    else
        Level.WriteTo(nullptr);
}

void ProposalTray::Settle(std::string_view operation, std::string_view done)
{
    const LevelDraft* draft = Level.PreviewedDraft();
    if (draft == nullptr)
        return;
    const std::string label = draft->Label;
    const JsonValue answer =
        Operations.Invoke(operation, JsonValue(JsonValue::Object{ { "draft", JsonValue(draft->Id) } }));
    const JsonValue* message = answer.Find("message");
    if (message != nullptr && message->IsString())
        Notices.Post(message->AsString(), NoticeTone::Warning, NoticeClockNow());
    else
        Notices.Post(std::string(done) + " '" + label + "'", NoticeTone::Done, NoticeClockNow());
}
