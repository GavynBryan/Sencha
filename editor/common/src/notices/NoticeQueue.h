#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

enum class NoticeTone : std::uint8_t
{
    Info,
    Done,
    Warning,
    Failed,
};

// Word that something just happened: saved, cooked, undone over there. Gone on
// its own after a moment; nothing to dismiss.
struct Notice
{
    std::string Text;
    NoticeTone Tone = NoticeTone::Info;
    double PostedAt = 0.0;
};

//=============================================================================
// NoticeQueue
// The notices worth showing at a given moment, newest last. Time is passed in,
// so what shows is a function of the posts and a clock the caller decides.
//=============================================================================
// Seconds on the clock the editor posts and shows notices by.
[[nodiscard]] double NoticeClockNow();

class NoticeQueue
{
public:
    // How long a notice shows, and how many show at once.
    static constexpr double kLifetimeSeconds = 4.0;
    static constexpr std::size_t kVisible = 3;

    void Post(std::string text, NoticeTone tone, double now);
    // Drops what has expired and returns what is showing.
    [[nodiscard]] std::span<const Notice> Showing(double now);

private:
    std::vector<Notice> Notices;
};
