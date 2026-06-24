#pragma once

//=============================================================================
// GameplayTagContainer scene-serializer registration
//
// Registers an IComponentSerializer for GameplayTagContainer with the scene
// serializer. Tags persist by name (see GameplayTagSerialization.h), resolved
// through the GameplayTagRegistry stored as a world resource on the registry
// being (de)serialized. Call once at startup, after InitSceneSerializer().
//
// This lives in the framework, not the engine manifest: the engine's
// EngineSceneComponents must not name gameplay types (D-J).
//=============================================================================

void RegisterGameplayTagSerializer();
