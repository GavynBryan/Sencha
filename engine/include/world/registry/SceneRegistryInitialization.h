#pragma once

#include <anim/AnimationClipPlaybackRuntime.h>
#include <audio/AudioSourceRuntime.h>
#include <core/assets/AssetStoreTable.h>

struct Registry;

// Registers the scene component manifest and the resources those components
// resolve their handles through, so a bare Registry can hold a loaded scene.
//
// This is the document/editor path: a registry built here is populated by the
// scene serializer. Streamed runtime content takes the other route, arriving as
// an EntityBuildPackage imported into RuntimeWorld storage partitions.
//
// The stores and services are optional because a caller that only inspects
// structure does not need to resolve assets; a component whose store is
// absent keeps an unowned handle.
void InitializeSceneRegistry(Registry& registry,
                             AssetStoreTable stores = {},
                             AudioSourceRuntime audio = {},
                             AnimationClipPlaybackRuntime animation = {});
