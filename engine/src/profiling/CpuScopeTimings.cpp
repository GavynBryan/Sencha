#include <profiling/CpuScopeTimings.h>

const char* ToString(CpuScope scope)
{
    switch (scope)
    {
    case CpuScope::Extraction:     return "Extract/Meshes";
    case CpuScope::LightSelection: return "Extract/Lights";
    case CpuScope::ShadowGather:   return "Extract/ShadowCasters";
    case CpuScope::ShadowResidency: return "Extract/ShadowResidency";
    case CpuScope::ShadowRecord:   return "Record/ShadowViews";
    case CpuScope::ForwardRecord:  return "Record/ForwardOpaque";
    case CpuScope::Count:          break;
    }
    return "?";
}
