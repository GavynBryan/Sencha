#pragma once

#include <assets/data/DataAssetCache.h>

#include <string>
#include <string_view>
#include <vector>

// Non-data assets such as the skeleton, and the definitions a fact schema was
// composed from, report OtherKind, without a residency status.
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

// The fact schema is followed by every definition it was composed from.
[[nodiscard]] std::vector<AnimationRigDependency> DescribeAnimationRigDependencies(
    const DataAssetCache& data, std::string_view rigPath);
