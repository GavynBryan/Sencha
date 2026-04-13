#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <raii/LifetimeHandle.h>
#include <batch/DataBatch.h>

struct SceneNodeCore
{
    uint32_t Id = 0;
    uint32_t Flags = 0;

    uint32_t ParentId = 0;
    std::vector<uint32_t> ChildIds;

    LifetimeHandle<DataBatchKey> LocalTransform;
    LifetimeHandle<DataBatchKey> WorldTransform;
};