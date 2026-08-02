#pragma once

#include <core/handle/Handle.h>
#include <core/handle/Owned.h>

using DataAssetHandle = Handle<struct DataAssetHandleTag>;
using DataAssetCacheHandle = Owned<DataAssetHandle>;
