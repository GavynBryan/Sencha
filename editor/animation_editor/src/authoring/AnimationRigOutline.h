#pragma once

#include <assets/data/DataAssetCache.h>

#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] std::vector<AnimationRigDependency> DescribeAnimationRigDependencies(
    const DataAssetCache& data, std::string_view rigPath);
