#include <profiling/RenderCapture.h>

#include <core/json/JsonStringify.h>
#include <core/json/JsonValue.h>

#include <sstream>

namespace
{
	constexpr double kToMs = 1000.0;

	// One flat name/number row per frame, shared by the JSON and CSV shapes
	// so the two formats cannot drift. Keys carry explicit units.
	struct FrameFields
	{
		const char* Key;
		double Value;
	};

	std::vector<FrameFields> BuildFrameFields(const RenderCapture::FrameRecord& record)
	{
		const TimingFrameSample& timing = record.Timing;
		const RenderStats& stats = record.Stats;
		std::vector<FrameFields> fields{
			{ "frame_index_count", static_cast<double>(stats.FrameIndex) },
			{ "raw_dt_ms", timing.RawDtSeconds * kToMs },
			{ "presentation_dt_ms", timing.PresentationDtSeconds * kToMs },
			{ "render_record_ms", timing.RenderRecordSeconds * kToMs },
			{ "acquire_ms", timing.AcquireSeconds * kToMs },
			{ "submit_ms", timing.SubmitSeconds * kToMs },
			{ "present_ms", timing.PresentSeconds * kToMs },
			{ "total_frame_ms", timing.TotalFrameSeconds * kToMs },
			{ "fixed_ticks_count", static_cast<double>(timing.FixedTicks) },
			{ "presented_bool", timing.Presented ? 1.0 : 0.0 },
			{ "swapchain_width_px", static_cast<double>(timing.SwapchainWidth) },
			{ "swapchain_height_px", static_cast<double>(timing.SwapchainHeight) },
			{ "present_mode_enum", static_cast<double>(timing.PresentMode) },
			{ "visible_objects_count", static_cast<double>(stats.VisibleObjects) },
			{ "draw_calls_count", static_cast<double>(stats.DrawCalls) },
			{ "submitted_triangles_count", static_cast<double>(stats.SubmittedTriangles) },
			{ "pipeline_switches_count", static_cast<double>(stats.PipelineSwitches) },
			{ "material_switches_count", static_cast<double>(stats.MaterialSwitches) },
			{ "instances_dropped_count", static_cast<double>(stats.InstancesDropped) },
			{ "lights_visible_count", static_cast<double>(stats.LightsVisible) },
			{ "lights_dropped_at_cap_count", static_cast<double>(stats.LightsDroppedAtCap) },
			{ "shadow_casting_lights_count", static_cast<double>(stats.ShadowCastingLights) },
			{ "shadow_views_rendered_count", static_cast<double>(stats.ShadowViewsRendered) },
			{ "point_shadow_faces_rendered_count", static_cast<double>(stats.PointShadowFacesRendered) },
			{ "shadow_caster_draws_count", static_cast<double>(stats.ShadowCasterDraws) },
			{ "shadow_casters_tested_count", static_cast<double>(stats.ShadowCastersTested) },
			{ "shadow_casters_visible_count", static_cast<double>(stats.ShadowCastersVisible) },
			{ "shadow_casters_dropped_count", static_cast<double>(stats.ShadowCastersDropped) },
			{ "shadow_slots_held_count", static_cast<double>(stats.ShadowSlotsHeld) },
			{ "shadow_cache_hits_count", static_cast<double>(stats.ShadowCacheHits) },
			{ "shadow_requests_denied_count", static_cast<double>(stats.ShadowRequestsDenied) },
			{ "atlas_tiles_1024_count", static_cast<double>(stats.AtlasTiles1024) },
			{ "atlas_tiles_512_count", static_cast<double>(stats.AtlasTiles512) },
			{ "atlas_tiles_256_count", static_cast<double>(stats.AtlasTiles256) },
			{ "point_shadow_cubes_held_count", static_cast<double>(stats.PointShadowCubesHeld) },
			{ "shadow_tile_bytes", static_cast<double>(stats.ShadowTileBytes) },
			{ "caster_diff_events_count", static_cast<double>(stats.CasterDiffEvents) },
			{ "probe_volumes_resident_count", static_cast<double>(stats.ProbeVolumesResident) },
			{ "scratch_high_water_bytes", static_cast<double>(stats.ScratchHighWaterBytes) },
			{ "scratch_used_bytes", static_cast<double>(stats.ScratchUsedBytes) },
			{ "scratch_bytes_per_frame", static_cast<double>(stats.ScratchBytesPerFrame) },
			{ "scratch_alloc_failures_count", static_cast<double>(stats.ScratchAllocFailures) },
			{ "passes_skipped_count", static_cast<double>(stats.PassesSkipped) },
		};
		return fields;
	}

	// GPU scope columns append after the fixed fields: "<scope>_gpu_ms",
	// -1 when the span was not collected that frame.
	double GpuScopeValue(const TimingFrameSample& timing, std::uint32_t index)
	{
		return timing.GpuScopes[index].Valid
			? static_cast<double>(timing.GpuScopes[index].Milliseconds)
			: -1.0;
	}

	std::string GpuScopeKey(std::uint32_t index)
	{
		std::string key = ToString(static_cast<GpuScope>(index));
		for (char& c : key)
		{
			if (c == '/')
				c = '_';
		}
		return key + "_gpu_ms";
	}

	// CPU scope columns append after the GPU ones: "<scope>_cpu_ms", negative
	// when the scope was not measured that frame.
	double CpuScopeValue(const TimingFrameSample& timing, std::uint32_t index)
	{
		return static_cast<double>(
			timing.CpuScopes.Get(static_cast<CpuScope>(index)));
	}

	std::string CpuScopeKey(std::uint32_t index)
	{
		std::string key = ToString(static_cast<CpuScope>(index));
		for (char& c : key)
		{
			if (c == '/')
				c = '_';
		}
		return key + "_cpu_ms";
	}
}

void RenderCapture::Start(std::size_t frameLimit)
{
	if (Records.empty())
		Records.resize(kDefaultCapacityFrames);
	Head = 0;
	Count = 0;
	RemainingFrames = frameLimit;
	Recording = true;
}

void RenderCapture::Append(const TimingFrameSample& timing, const RenderStats& stats)
{
	if (!Recording || Records.empty())
		return;

	Records[Head] = FrameRecord{ .Timing = timing, .Stats = stats };
	Head = (Head + 1) % Records.size();
	if (Count < Records.size())
		++Count;
	++Version;

	if (RemainingFrames > 0 && --RemainingFrames == 0)
		Recording = false;
}

const RenderCapture::FrameRecord& RenderCapture::GetChronological(std::size_t index) const
{
	const std::size_t start = Count == Records.size() ? Head : 0;
	return Records[(start + index) % Records.size()];
}

std::string RenderCapture::SerializeJson(
	const std::vector<MetadataPair>& cvarSnapshot) const
{
	JsonValue::Object envelope;
	envelope.emplace_back("schema_version",
	                      JsonValue(static_cast<double>(kSchemaVersion)));
	envelope.emplace_back("frame_count", JsonValue(static_cast<double>(Count)));

	JsonValue::Object environment;
	for (const MetadataPair& pair : Environment)
		environment.emplace_back(pair.first, JsonValue(pair.second));
	envelope.emplace_back("environment", JsonValue(std::move(environment)));

	JsonValue::Object cvars;
	for (const MetadataPair& pair : cvarSnapshot)
		cvars.emplace_back(pair.first, JsonValue(pair.second));
	envelope.emplace_back("cvars", JsonValue(std::move(cvars)));

	JsonValue::Array frames;
	frames.reserve(Count);
	for (std::size_t index = 0; index < Count; ++index)
	{
		const FrameRecord& record = GetChronological(index);
		JsonValue::Object frame;
		for (const FrameFields& field : BuildFrameFields(record))
			frame.emplace_back(field.Key, JsonValue(field.Value));
		for (std::uint32_t scope = 0; scope < kGpuScopeCount; ++scope)
			frame.emplace_back(GpuScopeKey(scope),
			                   JsonValue(GpuScopeValue(record.Timing, scope)));
		for (std::uint32_t scope = 0; scope < kCpuScopeCount; ++scope)
			frame.emplace_back(CpuScopeKey(scope),
			                   JsonValue(CpuScopeValue(record.Timing, scope)));
		frames.push_back(JsonValue(std::move(frame)));
	}
	envelope.emplace_back("frames", JsonValue(std::move(frames)));

	return JsonStringify(JsonValue(std::move(envelope)), /*pretty*/ false);
}

std::string RenderCapture::SerializeCsv() const
{
	std::ostringstream out;

	bool firstColumn = true;
	const auto column = [&](const std::string& text)
	{
		if (!firstColumn)
			out << ',';
		out << text;
		firstColumn = false;
	};

	if (Count > 0)
	{
		for (const FrameFields& field : BuildFrameFields(GetChronological(0)))
			column(field.Key);
		for (std::uint32_t scope = 0; scope < kGpuScopeCount; ++scope)
			column(GpuScopeKey(scope));
		for (std::uint32_t scope = 0; scope < kCpuScopeCount; ++scope)
			column(CpuScopeKey(scope));
	}
	out << '\n';

	for (std::size_t index = 0; index < Count; ++index)
	{
		const FrameRecord& record = GetChronological(index);
		firstColumn = true;
		for (const FrameFields& field : BuildFrameFields(record))
			column(std::to_string(field.Value));
		for (std::uint32_t scope = 0; scope < kGpuScopeCount; ++scope)
			column(std::to_string(GpuScopeValue(record.Timing, scope)));
		for (std::uint32_t scope = 0; scope < kCpuScopeCount; ++scope)
			column(std::to_string(CpuScopeValue(record.Timing, scope)));
		out << '\n';
	}
	return out.str();
}
