#include "MovementResolvePreview.h"

#include "../DataDocument.h"

#include <movement/MovementTags.h>

#include <algorithm>
#include <utility>

namespace
{
    constexpr std::string_view kFreeMode = "movement.mode.free";

    void CollectTags(const MovementLayerCondition& condition, std::vector<std::string>& out)
    {
        out.insert(out.end(), condition.AllTags.begin(), condition.AllTags.end());
        out.insert(out.end(), condition.AnyTags.begin(), condition.AnyTags.end());
        out.insert(out.end(), condition.NoneTags.begin(), condition.NoneTags.end());
    }

    void SortUnique(std::vector<std::string>& values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    }
}

void MovementResolvePreview::Update(const DataDocument& document,
                                    const DataAssetTypeRegistry& types)
{
    const bool sameSource = HasSource && SourcePath == document.VirtualPath();
    if (sameSource && SourceRevision == document.Revision())
        return;

    SourcePath = document.VirtualPath();
    SourceRevision = document.Revision();
    HasSource = true;
    Rebuild(document, types);
}

void MovementResolvePreview::Rebuild(const DataDocument& document,
                                     const DataAssetTypeRegistry& types)
{
    Profile.reset();
    Bound.reset();
    BindError.clear();

    const DataAssetTypeRegistration* type = types.Find(document.Subtype());
    const JsonValue* data = document.Data();
    if (type == nullptr || data == nullptr)
    {
        BindError = "no compiler is registered for this subtype";
        return;
    }

    DataAssetCompileResult compiled = type->Compile(*data);
    if (!compiled.IsValid())
    {
        BindError = compiled.Error.empty() ? "the document does not compile" : compiled.Error;
        return;
    }

    Profile = std::static_pointer_cast<const CompiledMovementProfile>(compiled.Value);
    if (Profile == nullptr)
    {
        BindError = "the compiler produced no movement profile";
        return;
    }

    CollectNames();

    MovementProfileBindResult result = BindMovementProfile(
        *Profile, TagRegistry,
        [this](std::string_view name) { return ResolveMode(name); });
    if (!result.IsValid())
    {
        BindError = result.Error;
        return;
    }
    Bound = std::move(*result.Profile);

    // Drop simulated tags the document no longer mentions, so a stale toggle
    // cannot keep matching a condition that was renamed or deleted.
    std::erase_if(Simulated.ActiveTags, [this](const std::string& tag)
    {
        return std::find(Tags.begin(), Tags.end(), tag) == Tags.end();
    });
}

void MovementResolvePreview::CollectNames()
{
    Tags.clear();
    Modes.clear();
    if (Profile == nullptr)
        return;

    for (const MovementProfileLayer& layer : Profile->Layers)
        CollectTags(layer.When, Tags);
    for (const MovementModeProfile& mode : Profile->Modes)
    {
        Modes.push_back(mode.Mode);
        Tags.insert(Tags.end(), mode.SustainAllTags.begin(), mode.SustainAllTags.end());
        Tags.insert(Tags.end(), mode.SustainAnyTags.begin(), mode.SustainAnyTags.end());
        Tags.insert(Tags.end(), mode.SustainNoneTags.begin(), mode.SustainNoneTags.end());
        for (const MovementProfileLayer& layer : mode.Layers)
        {
            CollectTags(layer.When, Tags);
            if (layer.When.Mode)
                Modes.push_back(*layer.When.Mode);
        }
    }
    for (const MovementProfileLayer& layer : Profile->Layers)
    {
        if (layer.When.Mode)
            Modes.push_back(*layer.When.Mode);
    }

    // The game's own movement tags are offered even when this document does not
    // mention them: they are the ones a character actually carries.
    (void)RegisterMovementTags(TagRegistry);
    Tags.push_back("movement.controlled");
    Tags.push_back("movement.grounded");
    Tags.push_back("movement.jump.requested");
    Tags.push_back("movement.jump.cooldown");
    Modes.emplace_back(kFreeMode);

    SortUnique(Tags);
    SortUnique(Modes);

    for (const std::string& tag : Tags)
        (void)TagRegistry.RegisterTag(tag);
    for (const std::string& mode : Modes)
    {
        if (!ModeIds.contains(mode))
            ModeIds.emplace(mode, static_cast<uint32_t>(ModeIds.size() + 1));
    }
}

LocomotionModeId MovementResolvePreview::ResolveMode(std::string_view name) const
{
    const auto found = ModeIds.find(std::string(name));
    return found == ModeIds.end() ? LocomotionModeId{} : LocomotionModeId{ found->second };
}

MovementResolveContext MovementResolvePreview::MakeResolveContext(
    SupportKind support,
    float immersion,
    const std::vector<std::string>& tags,
    GameplayTagContainer& storage) const
{
    storage = GameplayTagContainer{};
    for (const std::string& tag : tags)
    {
        if (const GameplayTagId id = TagRegistry.FindTag(tag); id.IsValid())
            storage.Grant(id);
    }

    MovementResolveContext context;
    context.Support = support;
    context.Immersion = immersion;
    context.Mode = Simulated.Mode.empty() ? ResolveMode(kFreeMode) : ResolveMode(Simulated.Mode);
    context.Tags = &storage;
    return context;
}

MovementResolveResult MovementResolvePreview::Resolve() const
{
    if (!Bound)
        return {};

    GameplayTagContainer tags;
    const MovementResolveContext context = MakeResolveContext(
        Simulated.Support, Simulated.Immersion, Simulated.ActiveTags, tags);
    return ResolveMovementTuning(*Bound, context, Simulated.MoveSpeed, true);
}
