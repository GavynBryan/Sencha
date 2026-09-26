#include "data/DataResidentSync.h"

#include "data/DataDocument.h"

#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>

#include <algorithm>

bool DataResidentSync::Push(const DataDocument& document)
{
    std::erase_if(Waiting, [&](const WaitingPush& push) { return push.Document == &document; });
    if (!IsResident(document))
    {
        Waiting.push_back({ &document, document.CommittedGeneration() });
        States[&document] = { DataResidentStatus::Pending, {} };
        return false;
    }
    if (!Apply(document))
        return false;
    (void)PushWaiting();
    return true;
}

bool DataResidentSync::PushWaiting()
{
    bool changed = false;
    // An applied version can load an asset another push waits on, so sweep until one pass applies nothing.
    for (bool progressed = true; progressed;)
    {
        progressed = false;
        for (auto push = Waiting.begin(); push != Waiting.end();)
        {
            const DataDocument& document = *push->Document;
            if (document.CommittedGeneration() != push->Generation)
            {
                States.erase(&document);
                push = Waiting.erase(push);
                continue;
            }
            if (!IsResident(document))
            {
                ++push;
                continue;
            }
            push = Waiting.erase(push);
            if (Apply(document))
                changed = progressed = true;
        }
    }
    return changed;
}

void DataResidentSync::Forget(const DataDocument& document)
{
    std::erase_if(Waiting, [&](const WaitingPush& push) { return push.Document == &document; });
    States.erase(&document);
}

void DataResidentSync::RestoreFromFile(const DataDocument& document)
{
    Forget(document);
    if (!IsResident(document))
        return;
    const AssetRecord* record = Assets.Registry.FindByPath(document.VirtualPath());
    AssetStaging staged = Assets.Assets.LoaderFor(AssetType::Data)->LoadStaged(*record, Assets.Assets.DefaultSource());
    if (staged.IsValid())
        (void)Assets.Assets.Reload(std::move(staged));
}

const DataResidentState* DataResidentSync::StateOf(const DataDocument& document) const
{
    const auto found = States.find(&document);
    return found == States.end() ? nullptr : &found->second;
}

bool DataResidentSync::IsResident(const DataDocument& document) const
{
    return Assets.Registry.FindByPath(document.VirtualPath()) != nullptr
        && Assets.Assets.IsResident(document.VirtualPath(), AssetType::Data);
}

bool DataResidentSync::Apply(const DataDocument& document)
{
    const AssetRecord* record = Assets.Registry.FindByPath(document.VirtualPath());
    AssetStaging staged = Assets.StageDataRoot(*record, document.CommittedRoot());
    DataResidentState& state = States[&document];
    if (!staged.IsValid())
    {
        state = { DataResidentStatus::KeptLastValid, std::move(staged.Error) };
        return false;
    }
    if (!Assets.Assets.Reload(std::move(staged)))
    {
        state = { DataResidentStatus::KeptLastValid, "a dependency of the working version did not load" };
        return false;
    }
    state = { DataResidentStatus::Current, {} };
    return true;
}
