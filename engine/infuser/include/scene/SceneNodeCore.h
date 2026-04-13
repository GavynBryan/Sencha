#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <math/Transform2.h>
#include <batch/DataBatch.h>
#include <raii/DataBatchHandle.h>

struct SceneNodeCore
{
    uint32_t Id = 0;
    uint32_t Flags = 0;

    uint32_t ParentId = 0;
    std::vector<uint32_t> ChildIds;

    DataBatchHandle<Transform2f> LocalTransform;
    DataBatchHandle<Transform2f> WorldTransform;
};
