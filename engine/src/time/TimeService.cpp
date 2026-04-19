#include <time/TimeService.h>
#include <algorithm>

TimeService::TimeService()
	: LastTime(Clock::now())
{
}

FrameClock TimeService::Advance()
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

	const float activeScale = GetTimescale();
	float scaledDelta = unscaledDelta * activeScale;
	UnscaledElapsedTime += unscaledDelta;
	ElapsedTime += scaledDelta;
	++FrameIndex;

	return FrameClock{
		.Dt              = scaledDelta,
		.UnscaledDt      = unscaledDelta,
		.Elapsed         = ElapsedTime,
		.UnscaledElapsed = UnscaledElapsedTime,
		.Timescale       = activeScale,
		.FrameIndex      = FrameIndex,
	};
}

void TimeService::ResetToNow()
{
	LastTime = Clock::now();
	FirstFrame = true;
}

TimescaleHandle TimeService::PushTimescale(float scale)
{
	TimescaleHandle handle{ NextHandleId++ };
	TimescaleStack.push_back({ handle.Id, scale });
	return handle;
}

void TimeService::PopTimescale(TimescaleHandle handle)
{
	if (!handle.IsValid())
		return;

	for (auto it = TimescaleStack.begin(); it != TimescaleStack.end(); ++it)
	{
		if (it->Id == handle.Id)
		{
			TimescaleStack.erase(it);
			return;
		}
	}
}

float TimeService::GetTimescale() const
{
	if (!TimescaleStack.empty())
		return TimescaleStack.back().Scale;
	return FlatTimescale;
}
