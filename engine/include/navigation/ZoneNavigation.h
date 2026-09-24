#pragma once

#include <gameplay_tags/GameplayTagId.h>
#include <navigation/NavigationFile.h>
#include <navigation/NavigationIds.h>
#include <navigation/NavigationTypes.h>
#include <zone/ZoneId.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

class GameplayTagRegistry;
class NavTileMesh;
class RuntimeWorld;
struct RuntimeZoneRecord;

// A navigation link as the runtime holds it, with its current state.
struct NavLinkInfo
{
    NavLinkId Id;
    // Invalid when no gameplay tag of the authored name is registered; such a
    // link is never usable.
    GameplayTagId Traversal;
    std::uint32_t Directions = 0;
    float BaseCost = 1.0f;
    float EntryRadius = 0.5f;
    Vec3d Entry = Vec3d::Zero();
    Vec3d Exit = Vec3d::Zero();
    bool Enabled = true;
    float CostScale = 1.0f;
    // Bumped when usability, cost, or attachment changes; see NavValidateRoute.
    std::uint32_t Revision = 1;
};

// One resident zone's navigation: a tiled mesh per build profile, the links
// attached to each, and runtime link state. A zone resource; queries only read
// it. See docs/navigation/runtime.md.
class ZoneNavigation
{
public:
    ZoneNavigation();
    ~ZoneNavigation();
    ZoneNavigation(ZoneNavigation&&) noexcept;
    ZoneNavigation& operator=(ZoneNavigation&&) noexcept;
    ZoneNavigation(const ZoneNavigation&) = delete;
    ZoneNavigation& operator=(const ZoneNavigation&) = delete;

    // Takes a fresh generation, so references into an earlier load are stale.
    // Names bind to gameplay tags here; unbound ones are reported in
    // Diagnostics(). False when no profile could be loaded.
    [[nodiscard]] bool Load(ZoneId zone, NavigationFile file,
                            const GameplayTagRegistry* tags);

    [[nodiscard]] ZoneId Zone() const;
    [[nodiscard]] std::uint32_t Generation() const;

    [[nodiscard]] std::size_t ProfileCount() const;
    [[nodiscard]] std::optional<std::uint16_t> FindProfile(GameplayTagId profile) const;
    [[nodiscard]] const std::string& ProfileName(std::uint16_t profile) const;
    [[nodiscard]] const NavTileMesh& Mesh(std::uint16_t profile) const;

    [[nodiscard]] std::span<const GameplayTagId> AreaTags() const;

    [[nodiscard]] std::size_t LinkCount() const;
    [[nodiscard]] std::optional<std::uint32_t> FindLink(NavLinkId id) const;
    [[nodiscard]] const NavLinkInfo& Link(std::uint32_t index) const;
    // Owner thread, between queries. Bumps the revision only on a change.
    void SetLinkState(std::uint32_t index, bool enabled, float costScale);

    [[nodiscard]] std::span<const std::string> Diagnostics() const;

    // Backend state, complete only inside the navigation module.
    struct Backend;
    [[nodiscard]] Backend& GetBackend() { return *Impl; }
    [[nodiscard]] const Backend& GetBackend() const { return *Impl; }

private:
    std::unique_ptr<Backend> Impl;
};

// The zone's cooked navigation, located beside its cooked scene by the same
// convention as probe volumes: "<scene stem>/navigation.snav". Task-thread safe.
// Returns false with an empty error when the zone simply has no navigation.
[[nodiscard]] bool ReadZoneNavigationFile(const std::string& cookedScenePath,
                                          NavigationFile& file, std::string* error);

// The navigation of a resident zone (dormant included), or null.
[[nodiscard]] ZoneNavigation* FindZoneNavigation(RuntimeWorld& runtime, ZoneId zone);
[[nodiscard]] const ZoneNavigation* FindZoneNavigation(const RuntimeWorld& runtime, ZoneId zone);

// Builds the zone's navigation and registers it as a zone resource, so it
// lives exactly as long as the zone. Owner thread, while the zone is attaching.
// Returns the registered navigation, or null when nothing loadable remained.
ZoneNavigation* AttachZoneNavigation(RuntimeWorld& runtime, RuntimeZoneRecord& zone,
                                     NavigationFile file);
