#include "CookTransport.h"

#include "EditorCookRuntime.h"
#include "actions/ActionOffer.h"
#include "ui/CookProfilesModal.h"

#include <string>
#include <utility>
#include <vector>

CookTransport::CookTransport(EditorCookRuntime& cook, CookProfilesModal* profiles, std::function<void()> settle)
    : Cook(cook)
    , Profiles(profiles)
    , Settle(std::move(settle))
{
}

void CookTransport::Offer(ActionCatalog& catalog, ActionContext& context)
{
    const ActionOffers offer{ catalog, context, "Transport" };
    const auto idle = [this] { return !Cook.IsActive(); };
    const auto playing = [this] { return Cook.Player().IsPlaying(); };
    offer({ .Id = "transport.cook", .Label = "Cook" }, { .Invoke = [this] { Settle(); Cook.Start(); }, .Enabled = idle });
    offer({ .Id = "transport.rebuild", .Label = "Cook From Scratch" },
          { .Invoke = [this] { Settle(); Cook.Start(/*rebuild*/ true); }, .Enabled = idle });
    offer({ .Id = "transport.cancel", .Label = "Cancel Cook" },
          { .Invoke = [this] { Cook.Cancel(); }, .Enabled = [this] { return Cook.IsActive(); } });
    offer({ .Id = "transport.play", .Label = "Play" },
          { .Invoke = [this] { Cook.Player().Play(Cook.Player().LastCookedMap()); },
            .Enabled = [playing] { return !playing(); } });
    offer({ .Id = "transport.stop", .Label = "Stop" },
          { .Invoke = [this] { Cook.Player().Stop(); }, .Enabled = playing });
    offer({ .Id = "transport.profiles", .Label = "Cook Profiles..." },
          { .Invoke = [this] { if (Profiles != nullptr) Profiles->Open(); },
            .Enabled = [this] { return Profiles != nullptr; } });
    OfferProfiles(catalog, context);
}

void CookTransport::Update(ActionCatalog& catalog, ActionContext& context)
{
    if (Profiles == nullptr)
        return;
    Profiles->Update();
    if (Profiles->Editor().Revision() != OfferedRevision)
    {
        OfferProfiles(catalog, context);
        OfferedRevision = Profiles->Editor().Revision();
    }
}

void CookTransport::OfferProfiles(ActionCatalog& catalog, ActionContext& context)
{
    const std::vector<CookProfile> profiles = Cook.GetSession().AvailableProfiles();
    std::vector<ActionDefinition> members;
    for (const CookProfile& profile : profiles)
        members.push_back(
            { .Id = "transport.profile." + profile.Id, .Label = "Cook with " + profile.Name, .Category = "Transport" });
    ProfileActions.Offer(catalog, context, std::move(members), [this, &profiles](std::size_t index) {
        const std::string& id = profiles[index].Id;
        return ActionBinding{ .Invoke = [this, id] { Cook.SelectProfile(id); },
                              .Checked = [this, id] { return Cook.SelectedProfileId() == id; } };
    });
}

WorkspaceTransportFacet CookTransport::Facet() const
{
    const CookSession::Status status = Cook.GetSession().GetStatus();
    if (Cook.Player().IsPlaying())
        return { .State = TransportState::Playing };
    if (status.Active)
    {
        std::string text = status.ProfileName;
        if (!status.StepName.empty())
            text += ": " + status.StepName;
        if (status.TotalSteps > 0)
            text += " (" + std::to_string(status.CompletedSteps) + "/" + std::to_string(status.TotalSteps) + ")";
        return { .State = TransportState::Cooking, .Readout = std::move(text) };
    }
    if (!status.LastError.empty())
        return { .State = TransportState::Failed, .Readout = status.LastError };
    return { .State = TransportState::Idle };
}
