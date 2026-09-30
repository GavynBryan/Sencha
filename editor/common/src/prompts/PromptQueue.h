#pragma once

#include "actions/ActionTone.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// One answer a prompt offers.
struct PromptChoice
{
    std::string Key;
    std::string Label;
    ActionTone Tone = ActionTone::Normal;
};

// A question that holds an operation until it is answered: whether to save
// before closing, which version of a file to keep. Items are what it is about.
struct PromptRequest
{
    std::string Title;
    std::string Message;
    std::vector<std::string> Items = {};
    std::vector<PromptChoice> Choices = {};
    // Told the key of the choice made. The choice keyed "cancel", when there is
    // one, is also what dismissing the prompt answers.
    std::function<void(std::string_view)> Answer = {};
};

//=============================================================================
// PromptQueue
//
// Questions waiting on the person, one at a time, oldest first. Whoever needs
// an answer asks through an Asker and is told later; whatever presents prompts
// shows the one in front. Neither knows how the other is built.
//=============================================================================
class PromptQueue
{
public:
    // What asks. Its prompts are withdrawn unanswered when it goes, so an
    // answer never reaches something that has closed. The queue outlives it.
    class Asker
    {
    public:
        Asker() = default;
        ~Asker();
        Asker(Asker&& other) noexcept;
        Asker& operator=(Asker&& other) noexcept;
        Asker(const Asker&) = delete;
        Asker& operator=(const Asker&) = delete;

        void Ask(PromptRequest request);
        [[nodiscard]] bool IsAsking() const;

    private:
        friend class PromptQueue;
        Asker(PromptQueue& queue, std::uint32_t id) : Queue(&queue), Id(id) {}

        PromptQueue* Queue = nullptr;
        std::uint32_t Id = 0;
    };

    PromptQueue() = default;
    PromptQueue(const PromptQueue&) = delete;
    PromptQueue& operator=(const PromptQueue&) = delete;

    [[nodiscard]] Asker MakeAsker() { return Asker(*this, ++LastAsker); }

    [[nodiscard]] const PromptRequest* Front() const { return Pending.empty() ? nullptr : &Pending.front().Request; }
    // Takes the front prompt away and tells its asker. A key the prompt does
    // not offer is refused, and the prompt stays.
    bool Answer(std::string_view key);
    // Answers the front prompt's "cancel", when it has one.
    bool Dismiss() { return Answer("cancel"); }

private:
    struct Entry
    {
        std::uint32_t Asker;
        PromptRequest Request;
    };

    void Withdraw(std::uint32_t asker);

    std::deque<Entry> Pending;
    std::uint32_t LastAsker = 0;
};
