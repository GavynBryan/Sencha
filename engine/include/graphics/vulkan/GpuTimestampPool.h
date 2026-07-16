#pragma once

#include <profiling/RenderInstrumentation.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

//=============================================================================
// GpuTimestampPool
//
// Timestamp query pools for the fixed GpuScope set, one VkQueryPool per
// frame in flight. Configure() stores the device once; the pools themselves
// are created lazily the first time a frame begins with the mode at Gpu or
// above and kept until Destroy() (destroy/recreate churn buys nothing).
//
// Per frame while active: BeginFrame resets this slot's queries inside the
// command buffer and collects the slot's previous results first. Collection
// runs at frame begin because VulkanFrameService has already fence-waited
// this slot there, so vkGetQueryPoolResults never needs WAIT; spans whose
// pair is unavailable read as invalid. Devices without timestamp support
// leave the pool permanently inert.
//=============================================================================
class GpuTimestampPool
{
public:
	void Configure(VkDevice device, float timestampPeriodNs,
	               std::uint32_t framesInFlight);
	// Destroys the query pools; safe to call with none created. Must run
	// before the device is destroyed.
	void Destroy();

	// Collects the slot's previous results into the last-scope snapshot,
	// then resets the slot's queries for this frame's writes. Creates the
	// query pools on first use.
	void BeginFrame(VkCommandBuffer cmd, std::uint32_t frameInFlightIndex);
	void BeginScope(VkCommandBuffer cmd, GpuScope scope);
	void EndScope(VkCommandBuffer cmd, GpuScope scope);

	// The most recently collected spans (from the oldest in-flight frame).
	[[nodiscard]] const std::array<GpuScopeSpan, kGpuScopeCount>& LastScopes() const
	{
		return Collected;
	}
	[[nodiscard]] bool HasPools() const { return !Pools.empty(); }

private:
	[[nodiscard]] bool EnsurePools();
	void Collect(std::uint32_t frameInFlightIndex);

	VkDevice Device = VK_NULL_HANDLE;
	float TimestampPeriodNs = 0.0f;
	std::uint32_t FramesInFlight = 0;

	std::vector<VkQueryPool> Pools;
	// Which scopes wrote both timestamps, per frame slot, so collection only
	// reads pairs that exist.
	std::vector<std::uint32_t> WrittenMasks;
	std::uint32_t CurrentFrame = 0;
	std::array<GpuScopeSpan, kGpuScopeCount> Collected{};
};
