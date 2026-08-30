#pragma once

#include <world/serialization/IComponentSerializer.h>

#include <memory>

//=============================================================================
// MovementTuningSource scene serializer
//
// The component holds a handle into the data-asset cache, which is a fact
// about one process's residency. Content states the profile as an asset path,
// and the load resolves it -- handing the reference to the component, whose
// lifecycle hooks own it from then on.
//
// A TypeSchema could almost describe this: an asset-tagged field would carry
// the path. It cannot, because the field codecs resolve typed caches the front
// door names, and structured data is the one kind it does not. This resolves
// it through the type-erased lease instead.
//
// Hand this to ComponentRegistrar::AddSerializer beside the component's Add.
//=============================================================================

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeMovementTuningSourceSerializer();
