#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

// Observers that stay subscribed exactly as long as the subscriber keeps the
// token Subscribe hands back. Dropping the token is unsubscribing, so an
// observer can never be called after the object it captured is gone.
template <typename... Args>
class WeakObserverList
{
public:
    using Fn = std::function<void(Args...)>;

    [[nodiscard]] std::shared_ptr<Fn> Subscribe(Fn fn)
    {
        auto token = std::make_shared<Fn>(std::move(fn));
        Observers.push_back(token);
        return token;
    }

    // An observer subscribed while this runs hears from the next notify on.
    void Notify(Args... args)
    {
        std::erase_if(Observers, [](const std::weak_ptr<Fn>& observer) { return observer.expired(); });
        const std::size_t count = Observers.size();
        for (std::size_t i = 0; i < count; ++i)
            if (std::shared_ptr<Fn> fn = Observers[i].lock())
                (*fn)(args...);
    }

private:
    std::vector<std::weak_ptr<Fn>> Observers;
};
