#include <input/InputBindingCache.h>

#include <input/ShellInputActions.h>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace
{
// Whether an entry in this state has tables worth serving.
bool HasTables(InputBindState state)
{
    return state == InputBindState::Current || state == InputBindState::Stale;
}

// Puts the shell's context at the front of the claim order, holding the
// author's bindings for shell actions plus the engine's defaults for the ones
// they left alone.
//
// First because its priority is above the reserved line no authored context may
// cross, so backing out of gameplay can never be shadowed by a gameplay binding
// on the same key.
void EmitShellContext(BoundInputProfile& out,
                      const std::vector<InputBinding>& authored,
                      const std::array<bool, static_cast<std::size_t>(ShellAction::Count)>& rebound)
{
    std::vector<InputBinding> bindings = authored;
    for (const InputBinding& fallback : ShellDefaultBindings())
    {
        if (fallback.ActionIndex < rebound.size() && rebound[fallback.ActionIndex])
            continue;
        bindings.push_back(fallback);
    }

    InputContextDefinition shell;
    shell.Name = std::string(kShellContextName);
    shell.Priority = kShellContextPriority;
    shell.IsShell = true;
    shell.FirstBinding = static_cast<std::uint32_t>(out.Bindings.size());
    shell.BindingCount = static_cast<std::uint32_t>(bindings.size());
    out.Bindings.insert(out.Bindings.end(), bindings.begin(), bindings.end());

    // The resolve pass walks contexts in order and claims as it goes, so the
    // shell has to be first in the vector, not merely highest-numbered.
    out.Contexts.insert(out.Contexts.begin(), std::move(shell));
}

// Compiles one profile against its action set into the tables a resolve pass
// walks. Writes into `actions` and `out` rather than the live entry, so a
// caller can throw the result away and keep what it had.
//
// Returns false only when nothing usable came out. Individual bindings that do
// not resolve are dropped and reported through `errors` while the rest bind.
bool BuildTables(InputActionRegistry& actions,
                 BoundInputProfile& out,
                 const CompiledInputProfile& profile,
                 const CompiledInputActionSet* actionSet,
                 std::vector<std::string>& errors)
{
    if (actionSet == nullptr)
    {
        errors.push_back(std::format("action set '{}' is missing or is not an {}",
                                     profile.ActionSetPath, kInputActionSetTypeName));
        return false;
    }

    // The shell's actions are declared first, in every profile, so a dense id
    // means the same thing everywhere and the engine can hold ShellAction as a
    // constant instead of resolving a name. An action set that redeclares one
    // is rebinding it, not adding it: the compiler already checked the shape
    // matches, so the engine's declaration stands and the authored one is
    // dropped here rather than minting a second id for the same name.
    std::vector<InputActionDefinition> merged;
    merged.reserve(ShellActionDefinitions().size() + actionSet->Actions.size());
    for (const InputActionDefinition& shell : ShellActionDefinitions())
        merged.push_back(shell);
    for (const InputActionDefinition& authored : actionSet->Actions)
    {
        if (FindShellAction(authored.Name) == nullptr)
            merged.push_back(authored);
    }

    std::string error;
    if (!actions.Rebuild(merged, &error))
    {
        errors.push_back(std::move(error));
        return false;
    }

    // Sized to every slot the registry has ever minted, so a dense id stays a
    // valid index into it after an action set drops one of its actions. A
    // retired slot has no binding and resolves to zero forever.
    out.ActionTypes.assign(actions.SlotCount(), InputActionType::Digital);
    out.ActionFireModes.assign(actions.SlotCount(), InputActionFireMode::Pressed);
    for (const InputActionDefinition& definition : merged)
    {
        const std::size_t index = InputActionRegistry::IndexOf(actions.Find(definition.Name));
        out.ActionTypes[index] = definition.Type;
        out.ActionFireModes[index] = definition.Fire;
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

    // Bindings an author wrote for a shell action, collected out of whatever
    // context declared them and compiled into the shell's own context below.
    //
    // Re-homing them is load-bearing rather than tidy. Authored contexts stop
    // resolving while the shell has input suspended, so a `ui.back` rebound
    // inside a game's `gameplay` context would go silent the instant the player
    // paused -- and there would be no way to resume. The author is declaring a
    // replacement *binding*; the action's context, priority and lifetime stay
    // the engine's.
    std::vector<InputBinding> shellBindings;
    // Which shell actions an author bound, so the engine's default bindings for
    // those actions can be dropped. Replace rather than augment: a player who
    // rebinds Back to another key must not find Escape still working.
    std::array<bool, static_cast<std::size_t>(ShellAction::Count)> rebound{};

    for (const AuthoredInputContext* context : ordered)
    {
        InputContextDefinition compiled;
        compiled.Name = context->Name;
        compiled.Priority = context->Priority;
        compiled.FirstBinding = static_cast<std::uint32_t>(out.Bindings.size());

        // A binding that cannot resolve is dropped, and the rest of the profile
        // still binds. Refusing the whole profile over one mistake costs the
        // player every control they have, which is a far worse answer to a
        // typo than losing the one binding it is in.
        for (std::size_t index = 0; index < context->Bindings.size(); ++index)
        {
            const AuthoredInputBinding& authored = context->Bindings[index];
            const InputActionId action = actions.Find(authored.Action);
            if (!action.IsValid())
            {
                errors.push_back(std::format(
                    "context '{}' binding {} names no declared action ('{}')",
                    context->Name, index + 1, authored.Action));
                continue;
            }

            const InputActionType type = out.ActionTypes[InputActionRegistry::IndexOf(action)];
            if (!BindingProducesType(authored.Binding, type))
            {
                errors.push_back(std::format(
                    "context '{}' binding {} drives '{}' with a control that cannot produce "
                    "its value", context->Name, index + 1, authored.Action));
                continue;
            }

            InputBinding binding = authored.Binding;
            const std::size_t actionIndex = InputActionRegistry::IndexOf(action);
            binding.ActionIndex = static_cast<std::uint32_t>(actionIndex);

            if (actionIndex < static_cast<std::size_t>(ShellAction::Count))
            {
                rebound[actionIndex] = true;
                shellBindings.push_back(binding);
                continue;
            }

            out.Bindings.push_back(binding);
        }

        compiled.BindingCount =
            static_cast<std::uint32_t>(out.Bindings.size()) - compiled.FirstBinding;
        out.Contexts.push_back(std::move(compiled));
    }

    EmitShellContext(out, shellBindings, rebound);
    return true;
}
}

std::string DescribeBindErrors(const InputBindStatus& status)
{
    std::string message;
    for (const std::string& error : status.Errors)
    {
        if (!message.empty())
            message.append("; ");
        message.append(error);
    }
    return message;
}

InputBindingCache::InputBindingCache(DataAssetCache& dataAssets)
    : DataAssets(dataAssets)
{
}

InputBindingCache::Entry* InputBindingCache::Resolve(InputProfileHandle handle)
{
    if (!handle.IsValid())
        return nullptr;

    Entry& entry = Entries[handle.Value.ToToken()];
    if (!entry.ProfileLease)
    {
        const std::string_view path = DataAssets.GetName(handle.Value);
        if (!path.empty())
            entry.ProfileLease = DataAssets.AcquireOwned(path);
    }

    const CompiledInputProfile* profile =
        DataAssets.TryGet<CompiledInputProfile>(handle.Value, kInputProfileTypeName);

    // The action set is a separate asset with its own reload version, and an
    // edit to the profile can point it at a different set, so the lease follows
    // the path rather than being taken once and kept.
    bool actionSetRetargeted = false;
    if (profile != nullptr && profile->ActionSetPath != entry.ActionSetPath)
    {
        entry.ActionSetPath = profile->ActionSetPath;
        entry.ActionSetLease = DataAssets.AcquireOwned(entry.ActionSetPath);
        actionSetRetargeted = true;
    }

    const std::uint64_t profileVersion = DataAssets.GetReloadVersion(handle.Value);
    const std::uint64_t actionSetVersion =
        DataAssets.GetReloadVersion(entry.ActionSetLease.GetToken());

    // A failure that nothing has changed since is not worth retrying: it would
    // rebuild, fail, and rebuild the same error string on every frame.
    const bool needsRebuild = entry.State == InputBindState::Unbound
        || actionSetRetargeted
        || entry.ProfileVersion != profileVersion
        || entry.ActionSetVersion != actionSetVersion;
    if (!needsRebuild)
        return &entry;

    ++Rebuilds;
    entry.ProfileVersion = profileVersion;
    entry.ActionSetVersion = actionSetVersion;

    std::vector<std::string> errors;
    // Built beside the live tables rather than over them. The registry copy is
    // what carries each name's id forward; a rebuild that fails is discarded
    // whole, which is what leaves the previous controls exactly as they were.
    InputActionRegistry actions = entry.Actions;
    BoundInputProfile bound;
    bool built = false;

    if (profile == nullptr)
    {
        errors.push_back(
            std::format("data asset is stale or is not an {}", kInputProfileTypeName));
    }
    else
    {
        const CompiledInputActionSet* actionSet = DataAssets.TryGet<CompiledInputActionSet>(
            entry.ActionSetLease.GetToken(), kInputActionSetTypeName);
        built = BuildTables(actions, bound, *profile, actionSet, errors);
    }

    if (built)
    {
        entry.Actions = std::move(actions);
        entry.Profile = std::move(bound);
        entry.State = InputBindState::Current;
    }
    else
    {
        // Keeping the last good tables costs the author nothing and costs the
        // player nothing; dropping them would take every control away for as
        // long as the bad edit is on disk.
        entry.State = HasTables(entry.State) ? InputBindState::Stale : InputBindState::Failed;
    }

    if (errors != entry.Errors)
    {
        entry.Errors = std::move(errors);
        ++entry.ErrorRevision;
    }

    return &entry;
}

const BoundInputProfile* InputBindingCache::Get(InputProfileHandle handle)
{
    const Entry* entry = Resolve(handle);
    if (entry == nullptr || !HasTables(entry->State))
        return nullptr;
    return &entry->Profile;
}

const InputActionRegistry* InputBindingCache::GetActions(InputProfileHandle handle)
{
    const Entry* entry = Resolve(handle);
    if (entry == nullptr || !HasTables(entry->State))
        return nullptr;
    return &entry->Actions;
}

InputBindStatus InputBindingCache::Status(InputProfileHandle handle) const
{
    const auto it = Entries.find(handle.Value.ToToken());
    if (it == Entries.end())
        return InputBindStatus{};
    return InputBindStatus{ it->second.State, it->second.ErrorRevision, it->second.Errors };
}

void InputBindingCache::Clear()
{
    Entries.clear();
}
