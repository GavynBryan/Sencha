#pragma once

#include <cassert>

class AssetSystem;
class LoggingProvider;

//=============================================================================
// SceneSerializationContext
//
// Explicit dependencies used by scene field codecs to convert stable persisted
// references into runtime handles, and runtime handles back into stable refs.
//=============================================================================
struct SceneSerializationContext
{
    SceneSerializationContext(LoggingProvider& logging, AssetSystem* assets = nullptr)
        : Assets(assets)
        , Logging(&logging)
    {
        assert(Logging != nullptr);
    }

    AssetSystem* Assets = nullptr;
    LoggingProvider* Logging = nullptr;
};
