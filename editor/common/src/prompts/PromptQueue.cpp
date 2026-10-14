#include "prompts/PromptQueue.h"

#include <algorithm>
#include <utility>

PromptQueue::Asker::~Asker()
{
    if (Queue != nullptr)
        Queue->Withdraw(Id);
}

PromptQueue::Asker::Asker(Asker&& other) noexcept
    : Queue(std::exchange(other.Queue, nullptr))
    , Id(other.Id)
{
}

PromptQueue::Asker& PromptQueue::Asker::operator=(Asker&& other) noexcept
{
    if (this != &other)
    {
        if (Queue != nullptr)
            Queue->Withdraw(Id);
        Queue = std::exchange(other.Queue, nullptr);
        Id = other.Id;
    }
    return *this;
}

void PromptQueue::Asker::Ask(PromptRequest request)
{
    if (Queue != nullptr)
        Queue->Pending.push_back({ Id, ++Queue->Asked, std::move(request) });
}

bool PromptQueue::Asker::IsAsking() const
{
    return Queue != nullptr
        && std::ranges::any_of(Queue->Pending, [&](const Entry& entry) { return entry.Asker == Id; });
}

bool PromptQueue::Answer(std::string_view key, std::string_view written)
{
    if (Pending.empty())
        return false;
    const std::vector<PromptChoice>& choices = Pending.front().Request.Choices;
    if (std::ranges::none_of(choices, [&](const PromptChoice& c) { return c.Key == key; }))
        return false;
    // Taken off before the asker hears, so an answer that asks again lands
    // behind this one rather than in its place.
    PromptRequest answered = std::move(Pending.front().Request);
    Pending.pop_front();
    if (answered.Answer)
        answered.Answer(key, written);
    return true;
}

void PromptQueue::Withdraw(std::uint32_t asker)
{
    std::erase_if(Pending, [&](const Entry& entry) { return entry.Asker == asker; });
}
