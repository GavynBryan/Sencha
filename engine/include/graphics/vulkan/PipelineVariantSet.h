#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

//=============================================================================
// PipelineVariantSet
//
// A family of pipelines built from one base description plus a small
// enumerated axis, valid only for the key it was compiled against --
// attachment formats for the mesh passes, depth-bias values for the shadow
// pass.
//
// Four families hand-wrote the invalidation rule and did not agree on it. Two
// record the key while a variant may still have failed to compile, and rely on
// a separate completeness scan to notice; one commits all-or-nothing. Each
// invents its own impossible value -- VK_FORMAT_UNDEFINED, a -1.0f bias -- to
// stand for "nothing built yet". None of it is covered, because reaching any
// of it needs a device.
//
// The rule, stated once: a family is usable only when every variant in it
// exists, so the key is recorded only after all of them do, and a build that
// fails part-way leaves the set exactly as it was.
//
// Building is eager rather than per-variant on demand. Driver compilation
// costs tens of milliseconds per family, which is invisible at load and a
// visible hitch on the first frame that needs the variant.
//=============================================================================

// The key for a family whose pipelines depend only on what they render into.
struct AttachmentFormatKey
{
    VkFormat Color = VK_FORMAT_UNDEFINED;
    VkFormat Depth = VK_FORMAT_UNDEFINED;

    friend bool operator==(AttachmentFormatKey, AttachmentFormatKey) = default;
};

template <std::size_t Count, typename Key>
class PipelineVariantSet
{
public:
    static_assert(Count > 0, "a pipeline family with no variants has no purpose");

    // Builds the family for `key` unless it is already built for that key.
    // `build(index)` returns one variant's pipeline, or VK_NULL_HANDLE to fail
    // the whole family and leave the set untouched, so the next call retries.
    //
    // In the steady state `build` is not called at all, which is why a caller
    // may assemble its GraphicsPipelineDesc inside it: the vectors that
    // description holds are allocated on a rebuild, not once a frame.
    template <typename BuildFn>
    [[nodiscard]] bool Ensure(const Key& key, BuildFn&& build)
    {
        if (BuiltFor.has_value() && *BuiltFor == key)
            return true;

        std::array<VkPipeline, Count> built{};
        for (std::size_t index = 0; index < Count; ++index)
        {
            built[index] = build(index);
            if (built[index] == VK_NULL_HANDLE)
                return false;
        }

        Pipelines = built;
        BuiltFor = key;
        return true;
    }

    // The variant at `index`, or VK_NULL_HANDLE when the family is unbuilt or
    // the index is past its end. Out of range is a reachable case, not a
    // programming error: the mesh pass indexes its two-variant debug families
    // with a queue item's pipeline id, which is numbered for the wider opaque
    // family.
    //
    // No separate built check: the array is written only by a build that
    // completed, so an unbuilt family reads back null on its own. Callers on a
    // per-draw path get a bounds test and a load.
    [[nodiscard]] VkPipeline Get(std::size_t index) const
    {
        return index < Count ? Pipelines[index] : VK_NULL_HANDLE;
    }

    // Drops the family. The pipelines belong to VulkanPipelineCache for the
    // lifetime of the service and are not destroyed here.
    void Reset()
    {
        Pipelines.fill(VK_NULL_HANDLE);
        BuiltFor.reset();
    }

private:
    std::array<VkPipeline, Count> Pipelines{};
    // Engaged exactly when every variant is built. Holding the key here rather
    // than beside a separate built flag is what removes the need for an
    // impossible key value meaning "not built yet".
    std::optional<Key> BuiltFor;
};
