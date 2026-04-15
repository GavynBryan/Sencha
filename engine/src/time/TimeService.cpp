#include <time/TimeService.h>
#include <algorithm>

TimeService::TimeService()
	: LastTime(Clock::now())
{
}

FrameTime TimeService::Advance()
{
	TimePoint now = Clock::now();

	float unscaledDelta = 0.0f;
	if (!FirstFrame)
	{
		using FloatSeconds = std::chrono::duration<float>;
		unscaledDelta = std::chrono::duration_cast<FloatSeconds>(now - LastTime).count();
		unscaledDelta = std::min(unscaledDelta, MaxDeltaSeconds);
	}

	FirstFrame = false;
	LastTime = now;

	float scaledDelta = unscaledDelta * Timescale;
	UnscaledElapsedTime += unscaledDelta;
	ElapsedTime += scaledDelta;

	return FrameTime{
		.DeltaTime           = scaledDelta,
		.UnscaledDeltaTime   = unscaledDelta,
		.ElapsedTime         = ElapsedTime,
		.UnscaledElapsedTime = UnscaledElapsedTime,
		.Timescale           = Timescale,
	};
}
