#include <input/InputBindingCache.h>

#include <algorithm>
#include <format>
#include <utility>

InputBindingCache::InputBindingCache(DataAssetCache& dataAssets)
    : DataAssets(dataAssets)
{
}

bool InputBindingCache::Rebuild(Entry& entry, const CompiledInputProfile& profile)
{
    entry.Actions = InputActionRegistry{};
    entry.Profile = BoundInputProfile{};

    const CompiledInputActionSet* actionSet = DataAssets.TryGet<CompiledInputActionSet>(
        entry.ActionSetLease.GetToken(), kInputActionSetTypeName);
    if (actionSet == nullptr)
    {
        entry.Error = std::format("action set '{}' is missing or is not an {}",
                                  profile.ActionSetPath, kInputActionSetTypeName);
        return false;
    }

    for (const InputActionDefinition& definition : actionSet->Actions)
    {
        std::string error;
        if (!entry.Actions.Register(definition, &error))
        {
            entry.Error = std::move(error);
            return false;
        }
        entry.Profile.ActionTypes.push_back(definition.Type);
    }

    // Highest priority first: a resolve pass walks contexts in claim order, so
    // the ordering is settled here rather than on every pass.
    std::vector<const AuthoredInputContext*> ordered;
    ordered.reserve(profile.Contexts.size());
    for (const AuthoredInputContext& context : profile.Contexts)
        ordered.push_back(&context);
    std::sort(ordered.begin(), ordered.end(),
              [](const AuthoredInputContext* a, const AuthoredInputContext* b) {
                  return a->Priority > b->Priority;
              });

    for (const AuthoredInputContext* context : ordered)
    {
        InputContextDefinition compiled;
        compiled.Name = context->Name;
        compiled.Priority = context->Priority;
        compiled.FirstBinding = static_cast<std::uint32_t>(entry.Profile.Bindings.size());

        for (const AuthoredInputBinding& authored : context->Bindings)
        {
            const InputActionId action = entry.Actions.Find(authored.Action);
            if (!action.IsValid())
            {
                entry.Error = std::format("context '{}' binds unknown action '{}'",
                                          context->Name, authored.Action);
                return false;
            }

            const InputActionType type = entry.Profile.ActionTypes[InputActionRegistry::IndexOf(action)];
            if (!BindingProducesType(authored.Binding, type))
            {
                entry.Error = std::format(
                    "context '{}' binds action '{}' to a control that cannot produce its value",
                    context->Name, authored.Action);
                return false;
            }

            InputBinding binding = authored.Binding;
            binding.ActionIndex = static_cast<std::uint32_t>(InputActionRegistry::IndexOf(action));
            entry.Profile.Bindings.push_back(binding);
        }

        compiled.BindingCount =
            static_cast<std::uint32_t>(entry.Profile.Bindings.size()) - compiled.FirstBinding;
        entry.Profile.Contexts.push_back(std::move(compiled));
    }

    return true;
}

InputBindingCache::Entry* InputBindingCache::Resolve(InputProfileHandle handle, std::string* newError)
{
    if (newError != nullptr)
        newError->clear();
    if (!handle.IsValid())
    {
        if (newError != nullptr)
            *newError = "input profile handle is invalid";
        return nullptr;
    }

    const std::uint64_t key = handle.Value.ToToken();
    Entry& entry = Entries[key];
    if (!entry.ProfileLease)
    {
        const std::string_view path = DataAssets.GetName(handle.Value);
        if (!path.empty())
            entry.ProfileLease = DataAssets.AcquireOwned(path);
    }

    const CompiledInputProfile* profile =
        DataAssets.TryGet<CompiledInputProfile>(handle.Value, kInputProfileTypeName);

    // The action set is a separate asset with its own reload version, so a
    // change to either one has to rebind.
    if (profile != nullptr && !entry.ActionSetLease)
        entry.ActionSetLease = DataAssets.AcquireOwned(profile->ActionSetPath);

    const std::uint64_t profileVersion = DataAssets.GetReloadVersion(handle.Value);
    const std::uint64_t actionSetVersion = DataAssets.GetReloadVersion(entry.ActionSetLease.GetToken());
    const bool needsRebuild = !entry.Bound
        || entry.ProfileVersion != profileVersion
        || entry.ActionSetVersion != actionSetVersion;

    if (needsRebuild)
    {
        ++Rebuilds;
        entry.ProfileVersion = profileVersion;
        entry.ActionSetVersion = actionSetVersion;

        BoundInputProfile previous = std::move(entry.Profile);
        InputActionRegistry previousActions = std::move(entry.Actions);
        const bool wasBound = entry.Bound;

        entry.Error.clear();
        entry.ErrorDelivered = false;

        if (profile == nullptr)
        {
            entry.Error = std::format("data asset is stale or is not an {}", kInputProfileTypeName);
        }
        else if (Rebuild(entry, *profile))
        {
            entry.Bound = true;
        }

        // A failed rebind keeps the last good tables: dropping them would take
        // the player's controls away for as long as the bad edit is on disk.
        if (!entry.Error.empty() && wasBound)
        {
            entry.Profile = std::move(previous);
            entry.Actions = std::move(previousActions);
        }
    }

    if (!entry.Error.empty() && !entry.ErrorDelivered && newError != nullptr)
    {
        *newError = entry.Error;
        entry.ErrorDelivered = true;
    }

    return &entry;
}

const BoundInputProfile* InputBindingCache::Get(InputProfileHandle handle, std::string* newError)
{
    Entry* entry = Resolve(handle, newError);
    if (entry == nullptr || !entry->Bound)
        return nullptr;
    return &entry->Profile;
}

const InputActionRegistry* InputBindingCache::GetActions(InputProfileHandle handle, std::string* newError)
{
    Entry* entry = Resolve(handle, newError);
    if (entry == nullptr || !entry->Bound)
        return nullptr;
    return &entry->Actions;
}

void InputBindingCache::Clear()
{
    Entries.clear();
}
