#pragma once

#include <memory>
#include <utility>

// Owns one binding in a dispatcher. Inert once the dispatcher is gone, and
// carries its generation so a late Reset cannot remove a replacement.
// `Owner` provides Release(Key, Generation).
template<typename Owner, typename Key, typename Generation>
class AuthoredBindingToken
{
public:
    // Published by the owner, which nulls Target when destroyed.
    struct Link
    {
        Owner* Target = nullptr;
    };

    AuthoredBindingToken() = default;
    AuthoredBindingToken(std::weak_ptr<Link> link, Key key, Generation generation)
        : Link_(std::move(link))
        , Key_(key)
        , Generation_(generation)
    {
    }
    ~AuthoredBindingToken() { Reset(); }

    AuthoredBindingToken(const AuthoredBindingToken&) = delete;
    AuthoredBindingToken& operator=(const AuthoredBindingToken&) = delete;

    AuthoredBindingToken(AuthoredBindingToken&& other) noexcept { MoveFrom(std::move(other)); }
    AuthoredBindingToken& operator=(AuthoredBindingToken&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    // Idempotent.
    void Reset()
    {
        const std::shared_ptr<Link> link = Link_.lock();
        if (link != nullptr && link->Target != nullptr && Key_.IsValid())
            link->Target->Release(Key_, Generation_);
        Link_.reset();
        Key_ = {};
        Generation_ = {};
    }

    [[nodiscard]] bool IsValid() const { return Key_.IsValid() && !Link_.expired(); }
    [[nodiscard]] Key Bound() const { return Key_; }

private:
    void MoveFrom(AuthoredBindingToken&& other) noexcept
    {
        Link_ = std::move(other.Link_);
        Key_ = other.Key_;
        Generation_ = other.Generation_;
        other.Link_.reset();
        other.Key_ = {};
        other.Generation_ = {};
    }

    std::weak_ptr<Link> Link_;
    Key Key_;
    Generation Generation_;
};
