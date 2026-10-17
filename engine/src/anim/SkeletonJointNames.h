#pragma once

#include <anim/Skeleton.h>
#include <core/hash/TransparentStringHash.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>

// A skeleton's joints by name, for binding what content names by joint. A name
// two joints share names neither, since content cannot say which it meant.
class SkeletonJointNames
{
public:
    enum class Status : std::uint8_t
    {
        Found,
        Unknown,
        Ambiguous,
    };

    struct Match
    {
        Status Result = Status::Unknown;
        std::uint16_t Joint = 0;
    };

    SkeletonJointNames() = default;
    // Holds views of the skeleton's names, so it must not outlive it.
    explicit SkeletonJointNames(const SkeletonData& skeleton);

    [[nodiscard]] Match Find(std::string_view name) const;

private:
    static constexpr int kAmbiguous = -1;
    std::unordered_map<std::string_view, int, TransparentStringHash, std::equal_to<>> ByName;
};
