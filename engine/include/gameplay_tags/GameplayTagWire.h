#pragma once

#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>

struct ReplicationWireContext;

// A tag as a codec sends it: its name's wire key, which names the same tag on
// every machine, where the id is only this World's registration order. Zero for
// no tag, a tag this World does not know, or a key two names share.
[[nodiscard]] std::uint32_t WireTag(const ReplicationWireContext& context, GameplayTagId tag);
[[nodiscard]] GameplayTagId LocalTag(const ReplicationWireContext& context, std::uint32_t key);
