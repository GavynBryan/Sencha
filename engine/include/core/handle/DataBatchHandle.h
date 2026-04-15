#pragma once
#include <core/handle/LifetimeHandle.h>
#include <core/batch/DataBatchKey.h>

//=============================================================================
// DataBatchHandle<T>
//
// Typed alias for a DataBatch-owned value. GetToken() returns DataBatchKey.
// Distinct DataBatch value types produce distinct handle types.
//=============================================================================
template<typename T>
using DataBatchHandle = LifetimeHandle<T, DataBatchKey>;
