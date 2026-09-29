#pragma once

#include <assets/data/DataAssetCache.h>

#include <string>
#include <string_view>
#include <vector>

// Non-data assets such as the skeleton report OtherKind, without a residency status.
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

// Fact schemas are listed in extends order; a looping chain stops at the repeat.
[[nodiscard]] std::vector<AnimationRigDependency> DescribeAnimationRigDependencies(
    const DataAssetCache& data, std::string_view rigPath);
