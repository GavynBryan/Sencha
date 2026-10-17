#include "SkeletonJointNames.h"

SkeletonJointNames::SkeletonJointNames(const SkeletonData& skeleton)
{
    ByName.reserve(skeleton.Joints.size());
    for (std::size_t j = 0; j < skeleton.Joints.size(); ++j)
    {
        const auto [it, inserted] = ByName.emplace(skeleton.Joints[j].Name, static_cast<int>(j));
        if (!inserted)
            it->second = kAmbiguous;
    }
}

SkeletonJointNames::Match SkeletonJointNames::Find(std::string_view name) const
{
    const auto found = ByName.find(name);
    if (found == ByName.end())
        return {};
    if (found->second == kAmbiguous)
        return { .Result = Status::Ambiguous };
    return { .Result = Status::Found, .Joint = static_cast<std::uint16_t>(found->second) };
}
