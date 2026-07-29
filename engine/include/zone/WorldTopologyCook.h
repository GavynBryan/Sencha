#pragma once

#include <math/geometry/3d/Transform3d.h>
#include <zone/WorldConnectionComponents.h>
#include <zone/WorldPartitionManifest.h>

#include <cstdint>
#include <span>
#include <string>

struct AuthoredDockCookInput
{
    WorldDock Dock;
    Transform3f Transform;
    uint64_t AuthoredEntity = 0;
};

struct AuthoredLinkCookInput
{
    WorldLink Link;
    uint64_t AuthoredEntity = 0;
};

[[nodiscard]] bool CookWorldTopology(WorldPartitionManifest& manifest,
                                     std::span<const AuthoredDockCookInput> docks,
                                     std::span<const AuthoredLinkCookInput> links,
                                     std::string* error = nullptr);
