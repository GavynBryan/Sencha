#include "authoring/AnimationRigOutline.h"

#include <anim/AnimFactSchema.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>

#include <algorithm>

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

    std::vector<std::string> seen;
    std::string schema = rig->FactSchemaPath;
    int depth = 1;
    while (!schema.empty() && std::find(seen.begin(), seen.end(), schema) == seen.end())
    {
        seen.push_back(schema);
        rows.push_back({ depth == 1 ? "Facts" : "Extends", schema, depth,
                         StateOf(data, schema, kAnimFactSchemaType) });
        const AnimFactSchema* value = data.TryGet<AnimFactSchema>(data.Find(schema), kAnimFactSchemaType);
        if (value == nullptr)
            break;
        schema = value->Extends;
        ++depth;
    }

    if (!rig->RequestSchemaPath.empty())
        rows.push_back({ "Requests", rig->RequestSchemaPath, 1,
                         StateOf(data, rig->RequestSchemaPath, kAnimRequestSchemaType) });
    return rows;
}
