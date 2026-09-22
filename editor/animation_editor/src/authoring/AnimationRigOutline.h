#pragma once

#include <assets/data/DataAssetCache.h>

#include <string>
#include <string_view>
#include <vector>

// One asset a rig depends on, as the dependency outline lists it. Data assets
// report whether they are resident in the cache the preview binds against;
// the skeleton is another asset kind, and its row names it without a status.
struct AnimationRigDependency
{
    enum class State : unsigned char
    {
        Resident,
        Missing,
        WrongSubtype,
        OtherKind,
    };

    std::string Role;
    std::string Path;
    int Depth = 0;
    State Status = State::Resident;
};

// The rig, its skeleton, its fact schema chain in extends order, and its
// request schema. A chain that loops is listed up to the repeat.
[[nodiscard]] std::vector<AnimationRigDependency> DescribeAnimationRigDependencies(
    const DataAssetCache& data, std::string_view rigPath);
