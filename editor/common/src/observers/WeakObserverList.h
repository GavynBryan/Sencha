#pragma once

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

    void Notify(Args... args)
    {
        auto it = Observers.begin();
        while (it != Observers.end())
        {
            if (std::shared_ptr<Fn> fn = it->lock())
            {
                (*fn)(args...);
                ++it;
            }
            else
            {
                it = Observers.erase(it);
            }
        }
    }

private:
    std::vector<std::weak_ptr<Fn>> Observers;
};
