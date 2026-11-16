#pragma once

#include <world/build/EntityBuildPackage.h>
#include <world/scene/SceneInstance.h>

#include <cstdint>

// Gives every entity of a spawn's package the spawn's group identity. False when
// any entity already carries one, which a package built with
// SmapPackageOptions::StripAuthoredIdentity never does.
[[nodiscard]] inline bool StampSpawnGroup(EntityBuildPackage& package, SceneInstance group)
{
    bool stamped = true;
    for (std::uint32_t i = 0; i < package.EntityCount(); ++i)
        stamped = package.AddComponent(PackageEntityId{ i }, group) && stamped;
    return stamped;
}
