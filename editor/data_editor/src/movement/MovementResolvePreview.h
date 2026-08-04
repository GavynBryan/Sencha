#pragma once

#include <assets/data/DataAssetTypeRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <movement/MovementProfileData.h>
#include <movement/MovementProfileRuntime.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class DataDocument;

// The facts an author dials to ask "what does this profile do here?". Stands in
// for what the running game would supply: the physical support fact, immersion,
// the active mode, granted tags, and the MoveSpeed attribute that seeds the
// resolve.
struct MovementPreviewContext
{
    SupportKind Support = SupportKind::Stable;
    float Immersion = 0.0f;
    std::string Mode;
    std::vector<std::string> ActiveTags;
    float MoveSpeed = 4.5f;
};

// Compiles, binds, and resolves the document being edited so the authoring
// surfaces can show which layers match and what they produce.
//
// The bind needs tag and mode identities, which live in a running game's
// registries. The editor has none, so it mints them locally: every name the
// document mentions binds, plus the built-in movement.* hierarchy. That means a
// tag the real game never registers still previews here — the game's own
// binding error is the authority on that, not this preview.
//
// Owned by DataEditorServices: the simulated context is shared by the form and
// both movement panels, so it has one owner rather than a copy per panel.
class MovementResolvePreview
{
public:
    // Recompiles only when the document's content revision moves.
    void Update(const DataDocument& document, const DataAssetTypeRegistry& types);

    [[nodiscard]] bool IsBound() const { return Bound.has_value(); }
    [[nodiscard]] const std::string& Error() const { return BindError; }
    [[nodiscard]] const CompiledMovementProfile* Compiled() const { return Profile.get(); }
    [[nodiscard]] const BoundMovementProfile* BoundProfile() const
    {
        return Bound ? &*Bound : nullptr;
    }

    [[nodiscard]] MovementPreviewContext& Context() { return Simulated; }
    [[nodiscard]] const MovementPreviewContext& Context() const { return Simulated; }

    // Resolve under the simulated context with the per-layer trace collected.
    [[nodiscard]] MovementResolveResult Resolve() const;

    // Builds the resolve inputs for an arbitrary context, so the response
    // simulation can drive per-tick phases through the same binding.
    [[nodiscard]] MovementResolveContext MakeResolveContext(
        SupportKind support,
        float immersion,
        const std::vector<std::string>& tags,
        GameplayTagContainer& storage) const;

    [[nodiscard]] const std::vector<std::string>& KnownTags() const { return Tags; }
    [[nodiscard]] const std::vector<std::string>& KnownModes() const { return Modes; }

private:
    void Rebuild(const DataDocument& document, const DataAssetTypeRegistry& types);
    void CollectNames();
    [[nodiscard]] LocomotionModeId ResolveMode(std::string_view name) const;

    std::shared_ptr<const CompiledMovementProfile> Profile;
    std::optional<BoundMovementProfile> Bound;
    std::string BindError;

    GameplayTagRegistry TagRegistry;
    std::unordered_map<std::string, uint32_t> ModeIds;
    std::vector<std::string> Tags;
    std::vector<std::string> Modes;

    MovementPreviewContext Simulated;
    std::string SourcePath;
    uint64_t SourceRevision = 0;
    bool HasSource = false;
};
