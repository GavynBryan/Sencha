#pragma once

#include <cstdint>
#include <string>
#include <vector>

// What a workspace whose work runs over time has on its clock, for a deck to
// show, scrub and play: the clock's name, the span it covers and where it is,
// in seconds, and what is marked along it. The transport is the timeline.*
// actions (play, restart, step_back, step_forward, loop, slower, faster,
// run_ahead, live) and the clock switch timeline.source.*, which a workspace
// with a timeline claims.
enum class TimelineMarkShape : std::uint8_t
{
    Round,
    Diamond,
};

struct TimelineMark
{
    std::string Key;
    double Time = 0.0;
    std::string Label = {};
    TimelineMarkShape Shape = TimelineMarkShape::Round;
    bool Selected = false;
    bool Movable = false;
};

// A stretch of the clock one thing held, such as the rule winning a layer
// from one decision to the next.
struct TimelineSpan
{
    std::string Key;
    double Start = 0.0;
    double End = 0.0;
    std::string Label = {};
    bool Selected = false;
};

// A value read at a moment on the clock, for a lane that plots.
struct TimelineSample
{
    double Time = 0.0;
    double Value = 0.0;
};

// A row under the ruler: what held along the clock, span by span, or a value
// plotted along it, sample by sample, scaled to the largest.
struct TimelineLane
{
    std::string Label;
    std::vector<TimelineSpan> Spans = {};
    std::vector<TimelineSample> Samples = {};
};

struct WorkspaceTimelineFacet
{
    // Empty when the workspace keeps no time.
    std::string Clock = {};
    double Start = 0.0;
    double End = 0.0;
    double Time = 0.0;
    bool Playing = false;
    // Showing the running present rather than a moment scrubbed back to.
    bool Live = true;
    std::string Readout = {};
    std::vector<TimelineMark> Marks = {};
    std::vector<TimelineLane> Lanes = {};
};

// Where a drag along the deck is: pressed, moving, let go, or given up with
// nothing kept. A drag the workspace hears no end of (focus lost, Escape, its
// lens or clock switched) it cancels itself.
enum class TimelineDrag : std::uint8_t
{
    Begin,
    Move,
    End,
    Cancel,
};
