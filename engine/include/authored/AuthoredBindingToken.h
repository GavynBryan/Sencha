#pragma once

#include <memory>
#include <utility>

//=============================================================================
// AuthoredBindingToken
//
// What an implementation's owner holds, and gives back before the
// implementation is destroyed -- for a verb, a query, or an event
// subscription alike.
//
// The dispatcher publishes a control block the tokens watch, so a token that
// outlives its dispatcher is inert rather than a dangling pointer. That is the
// ownership contract, chosen rather than inherited from declaration order: a
// host that composes the dispatcher and the implementations in the same scope
// is correct, and so is one whose token member happens to be declared first.
//
// A token also carries the generation its binding had, so an owner that unbinds
// late cannot remove the replacement someone else bound in the meantime.
//
// `Owner` provides Release(Key, Generation), reachable by the token.
//=============================================================================
template<typename Owner, typename Key, typename Generation>
class AuthoredBindingToken
{
public:
    // What the owner publishes and the tokens watch. The owner nulls it when it
    // is destroyed.
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

    // Removes the binding this token minted, if it is still the one in place and
    // the owner is still alive. Idempotent.
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
