#pragma once

#include <cassert>

class AssetSystem;
class LoggingProvider;
class GameplayTagRegistry;
class AttributeRegistry;

//=============================================================================
// SceneSerializationContext
//
// Explicit dependencies used by scene field codecs to convert stable persisted
// references into runtime handles, and runtime handles back into stable refs.
// Session definitions (the gameplay-tag and attribute registries) are supplied
// here rather than recovered from entity storage; the caller resolves them from
// the session's global resources. They stay null for scenes with no gameplay-tag
// or attribute components.
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
    GameplayTagRegistry* GameplayTags = nullptr;
    AttributeRegistry* Attributes = nullptr;
};
