#include "authoring/AnimationRigOutline.h"

#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>


namespace
{
    AnimationRigDependency::State StateOf(const DataAssetCache& data, std::string_view path,
                                          std::string_view subtype)
    {
        const DataAssetHandle handle = data.Find(path);
        if (!handle.IsValid())
            return AnimationRigDependency::State::Missing;
        return data.GetSubtype(handle) == subtype ? AnimationRigDependency::State::Resident
                                                  : AnimationRigDependency::State::WrongSubtype;
    }
}

std::vector<AnimationRigDependency> DescribeAnimationRigDependencies(const DataAssetCache& data,
                                                                     std::string_view rigPath)
{
    std::vector<AnimationRigDependency> rows;
    rows.push_back({ "Rig", std::string(rigPath), 0, StateOf(data, rigPath, kAnimRigType) });
    const AnimRigData* rig = data.TryGet<AnimRigData>(data.Find(rigPath), kAnimRigType);
    if (rig == nullptr)
        return rows;

    if (!rig->SkeletonPath.empty())
        rows.push_back({ "Skeleton", rig->SkeletonPath, 1, AnimationRigDependency::State::OtherKind });

    if (!rig->FactSchemaPath.empty())
    {
        rows.push_back({ "Facts", rig->FactSchemaPath, 1, StateOf(data, rig->FactSchemaPath, kAnimFactSchemaType) });
        for (const std::string& source : data.GetCompositionSources(data.Find(rig->FactSchemaPath)))
            rows.push_back({ "Composed from", source, 2, AnimationRigDependency::State::OtherKind });
    }

    if (!rig->RequestSchemaPath.empty())
        rows.push_back({ "Requests", rig->RequestSchemaPath, 1,
                         StateOf(data, rig->RequestSchemaPath, kAnimRequestSchemaType) });
    return rows;
}
