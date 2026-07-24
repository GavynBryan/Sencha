#include <graphics/vulkan/GpuTimestampPool.h>

namespace
{
	// Two timestamps per scope: begin at even, end at odd.
	constexpr std::uint32_t kQueriesPerFrame = kGpuScopeCount * 2u;
}

void GpuTimestampPool::Configure(VkDevice device, float timestampPeriodNs,
                                 std::uint32_t framesInFlight)
{
	Device = device;
	TimestampPeriodNs = timestampPeriodNs;
	FramesInFlight = framesInFlight;
}

void GpuTimestampPool::Destroy()
{
	for (VkQueryPool pool : Pools)
	{
		if (pool != VK_NULL_HANDLE)
			vkDestroyQueryPool(Device, pool, nullptr);
	}
	Pools.clear();
	WrittenMasks.clear();
	Collected = {};
}

bool GpuTimestampPool::EnsurePools()
{
	if (!Pools.empty())
		return true;
	if (Device == VK_NULL_HANDLE || FramesInFlight == 0
	    || TimestampPeriodNs <= 0.0f)
	{
		return false;
	}

	Pools.resize(FramesInFlight, VK_NULL_HANDLE);
	WrittenMasks.assign(FramesInFlight, 0u);
	for (std::uint32_t index = 0; index < FramesInFlight; ++index)
	{
		VkQueryPoolCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		info.queryType = VK_QUERY_TYPE_TIMESTAMP;
		info.queryCount = kQueriesPerFrame;
		if (vkCreateQueryPool(Device, &info, nullptr, &Pools[index]) != VK_SUCCESS)
		{
			Destroy();
			return false;
		}
	}
	return true;
}

void GpuTimestampPool::BeginFrame(VkCommandBuffer cmd,
                                  std::uint32_t frameInFlightIndex)
{
	if (!EnsurePools() || frameInFlightIndex >= Pools.size())
		return;

	// This slot's fence was waited before recording began, so its previous
	// submission's queries are final.
	Collect(frameInFlightIndex);

	CurrentFrame = frameInFlightIndex;
	WrittenMasks[frameInFlightIndex] = 0u;
	vkCmdResetQueryPool(cmd, Pools[frameInFlightIndex], 0, kQueriesPerFrame);
}

void GpuTimestampPool::BeginScope(VkCommandBuffer cmd, GpuScope scope)
{
	if (Pools.empty() || CurrentFrame >= Pools.size())
		return;
	const auto index = static_cast<std::uint32_t>(scope);
	vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	                     Pools[CurrentFrame], index * 2u);
}

void GpuTimestampPool::EndScope(VkCommandBuffer cmd, GpuScope scope)
{
	if (Pools.empty() || CurrentFrame >= Pools.size())
		return;
	const auto index = static_cast<std::uint32_t>(scope);
	vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
	                     Pools[CurrentFrame], index * 2u + 1u);
	WrittenMasks[CurrentFrame] |= 1u << index;
}

void GpuTimestampPool::Collect(std::uint32_t frameInFlightIndex)
{
	Collected = {};
	const std::uint32_t written = WrittenMasks[frameInFlightIndex];
	if (written == 0u)
		return;

	// 64-bit value + 64-bit availability word per query.
	std::array<std::uint64_t, kQueriesPerFrame * 2u> results{};
	const VkResult status = vkGetQueryPoolResults(
		Device, Pools[frameInFlightIndex], 0, kQueriesPerFrame,
		sizeof(results), results.data(), 2u * sizeof(std::uint64_t),
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
	if (status != VK_SUCCESS && status != VK_NOT_READY)
		return;

	for (std::uint32_t index = 0; index < kGpuScopeCount; ++index)
	{
		if ((written & (1u << index)) == 0u)
			continue;
		const std::uint64_t beginValue = results[index * 4u];
		const std::uint64_t beginAvailable = results[index * 4u + 1u];
		const std::uint64_t endValue = results[index * 4u + 2u];
		const std::uint64_t endAvailable = results[index * 4u + 3u];
		if (beginAvailable == 0u || endAvailable == 0u || endValue < beginValue)
			continue;
		Collected[index] = GpuScopeSpan{
			.Milliseconds = static_cast<float>(
				static_cast<double>(endValue - beginValue)
				* static_cast<double>(TimestampPeriodNs) / 1.0e6),
			.Valid = true,
		};
	}
}
