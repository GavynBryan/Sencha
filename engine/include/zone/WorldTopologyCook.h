#pragma once

#include <core/identity/Id.h>
#include <math/geometry/3d/Transform3d.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionManifest.h>

#include <cstdint>
#include <span>
#include <string>

struct AuthoredDockCookInput
{
    WorldDock Dock{};
    Transform3f Transform{};
    uint64_t AuthoredEntity = 0;
};

struct AuthoredLinkCookInput
{
    WorldLink Link{};
    uint64_t AuthoredEntity = 0;
};

// A LinkAperture authored in a zone, with its world transform.
struct AuthoredApertureCookInput
{
    PersistentEntityId Id{};
    ZoneId Zone{};
    LinkAperture Aperture{};
    Transform3f Transform{};
};

// The aperture of that persistent id, or null; an invalid id finds none.
[[nodiscard]] const AuthoredApertureCookInput* FindAuthoredAperture(
    std::span<const AuthoredApertureCookInput> apertures, PersistentEntityId id);

// A link whose two apertures are both among `apertures`, with valid frames,
// cooks each frame onto its side's endpoint; any other link cooks as a demand
// edge. Whether a spatial link is well formed is ValidateAuthoredLinks' to say.
[[nodiscard]] bool CookWorldTopology(WorldPartitionManifest& manifest,
                                     std::span<const AuthoredDockCookInput> docks,
                                     std::span<const AuthoredLinkCookInput> links,
                                     std::span<const AuthoredApertureCookInput> apertures,
                                     std::string* error = nullptr);
