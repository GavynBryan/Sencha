#include "notices/NoticeQueue.h"

#include <algorithm>

void NoticeQueue::Post(std::string text, NoticeTone tone, double now)
{
    Notices.push_back(Notice{ std::move(text), tone, now });
    if (Notices.size() > kVisible)
        Notices.erase(Notices.begin(), Notices.end() - static_cast<std::ptrdiff_t>(kVisible));
}

std::span<const Notice> NoticeQueue::Showing(double now)
{
    std::erase_if(Notices, [now](const Notice& notice) { return now - notice.PostedAt >= kLifetimeSeconds; });
    return Notices;
}
